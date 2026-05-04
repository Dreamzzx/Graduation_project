from flask import Flask, render_template, request, jsonify, Response, session, redirect, url_for
from functools import wraps
import os
import json
import subprocess
import threading
import glob
import sys
import re
import psutil
import time
import platform
import hashlib
import secrets
import uuid

app = Flask(__name__)
app.secret_key = secrets.token_hex(32)

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STREAMS_DIR = os.path.join(PROJECT_ROOT, "streams")
SERVER_PROCESS = None
USERS_FILE = os.path.join(PROJECT_ROOT, "users.json")

STREAM_PROCESSES = {}

DEFAULT_USERS = {
    "root": {
        "password": hashlib.sha256("root".encode()).hexdigest(),
        "role": "admin"
    }
}

def init_users_file():
    if not os.path.exists(USERS_FILE):
        with open(USERS_FILE, 'w', encoding='utf-8') as f:
            json.dump(DEFAULT_USERS, f, indent=2)

def load_users():
    if not os.path.exists(USERS_FILE):
        init_users_file()
    with open(USERS_FILE, 'r', encoding='utf-8') as f:
        return json.load(f)

def save_users(users):
    with open(USERS_FILE, 'w', encoding='utf-8') as f:
        json.dump(users, f, indent=2)

def hash_password(password):
    return hashlib.sha256(password.encode()).hexdigest()

def login_required(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'user' not in session:
            if request.is_json:
                return jsonify({"success": False, "message": "请先登录"}), 401
            return redirect(url_for('login_page'))
        return f(*args, **kwargs)
    return decorated_function

def admin_required(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'user' not in session:
            if request.is_json:
                return jsonify({"success": False, "message": "请先登录"}), 401
            return redirect(url_for('login_page'))
        if session.get('role') != 'admin':
            if request.is_json:
                return jsonify({"success": False, "message": "权限不足，需要管理员权限"}), 403
            return redirect(url_for('index'))
        return f(*args, **kwargs)
    return decorated_function

SERVER_CONFIGS = {
    "mediamtx": {
        "name": "MediaMTX",
        "path": os.path.join(PROJECT_ROOT, "mediamtx", "mediamtx.exe"),
        "rtsp_port": 8554,
        "http_port": 8888,
        "hls_port": 8888
    }
}

SYSTEM_STATS = {
    "start_time": time.time(),
    "total_frames": 0,
    "total_detections": 0
}

def ensure_streams_dir():
    os.makedirs(STREAMS_DIR, exist_ok=True)

def get_video_devices():
    devices = []
    try:
        result = subprocess.run(
            ["ffmpeg", "-list_devices", "true", "-f", "dshow", "-i", "dummy"],
            capture_output=True, text=True, timeout=10,
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
        )
        for line in result.stderr.split('\n'):
            if '(video)' in line:
                match = re.search(r'"([^"]+)"', line)
                if match:
                    device_name = match.group(1).strip()
                    if device_name and '@' not in device_name and device_name not in devices:
                        devices.append(device_name)
    except Exception as e:
        print(f"获取设备失败: {e}")
    return devices

def get_device_capabilities(device_name):
    capabilities = {"resolutions": [], "framerates": [], "default_resolution": None, "default_framerate": None}
    try:
        result = subprocess.run(
            ["ffmpeg", "-f", "dshow", "-list_options", "true", "-i", f"video={device_name}"],
            capture_output=True, text=True, timeout=15,
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
        )
        resolutions = set()
        framerates = set()
        for line in result.stderr.split('\n'):
            if 'vcodec=mjpeg' in line.lower() or 'pixel_format' in line.lower():
                resolution_match = re.search(r'(\d+)x(\d+)', line)
                fps_match = re.search(r'fps=(\d+)', line)
                if resolution_match:
                    resolutions.add((int(resolution_match.group(1)), int(resolution_match.group(2))))
                if fps_match:
                    framerates.add(int(fps_match.group(1)))
        resolutions = sorted(resolutions, key=lambda x: x[0] * x[1], reverse=True)
        framerates = sorted(framerates, reverse=True)
        capabilities["resolutions"] = [{"width": w, "height": h, "label": f"{w}x{h}"} for w, h in resolutions]
        capabilities["framerates"] = list(framerates)
        if resolutions:
            capabilities["default_resolution"] = {"width": resolutions[0][0], "height": resolutions[0][1]}
        if framerates:
            capabilities["default_framerate"] = framerates[0]
    except Exception as e:
        print(f"获取设备参数失败: {e}")
    return capabilities

def get_available_models():
    models = []
    model_dir = os.path.join(PROJECT_ROOT, "model")
    if os.path.exists(model_dir):
        for f in os.listdir(model_dir):
            if f.endswith('.onnx') or f.endswith('.engine') or f.endswith('.pt'):
                models.append({
                    "name": f,
                    "path": f"model/{f}"
                })
    return models

def get_executable_path():
    search_paths = [
        os.path.join(PROJECT_ROOT, "build", "bin", "Debug", "VideoPush.exe"),
        os.path.join(PROJECT_ROOT, "build", "bin", "Release", "VideoPush.exe"),
        os.path.join(PROJECT_ROOT, "VideoPush.exe")
    ]
    for path in search_paths:
        if os.path.exists(path):
            return path
    return None

def load_stream_list():
    ensure_streams_dir()
    streams = []
    for f in sorted(glob.glob(os.path.join(STREAMS_DIR, "*.json"))):
        try:
            with open(f, 'r', encoding='utf-8') as fp:
                stream = json.load(fp)
                stream_id = os.path.splitext(os.path.basename(f))[0]
                stream["id"] = stream_id
                stream["_status"] = get_stream_status(stream_id)
                stream["_hls_url"] = get_stream_hls_url(stream_id)
                streams.append(stream)
        except:
            pass
    return streams

def load_stream(stream_id):
    path = os.path.join(STREAMS_DIR, f"{stream_id}.json")
    if os.path.exists(path):
        with open(path, 'r', encoding='utf-8') as f:
            return json.load(f)
    return None

def save_stream(stream_id, config):
    ensure_streams_dir()
    path = os.path.join(STREAMS_DIR, f"{stream_id}.json")
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(config, f, indent=4, ensure_ascii=False)

def delete_stream_file(stream_id):
    path = os.path.join(STREAMS_DIR, f"{stream_id}.json")
    if os.path.exists(path):
        os.remove(path)

def start_stream_process(stream_id):
    if stream_id in STREAM_PROCESSES and STREAM_PROCESSES[stream_id] and STREAM_PROCESSES[stream_id].poll() is None:
        return False, "该流已在运行中"
    
    stream_config = load_stream(stream_id)
    if not stream_config:
        return False, "流配置不存在"
    
    exe_path = get_executable_path()
    if not exe_path:
        return False, "未找到可执行文件"
    
    config_path = os.path.join(STREAMS_DIR, f"{stream_id}.json")
    log_path = os.path.join(STREAMS_DIR, f"{stream_id}.log")
    
    try:
        proc = subprocess.Popen(
            [exe_path, "--config", config_path],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
            cwd=PROJECT_ROOT,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == 'win32' else 0
        )
        
        def write_log():
            with open(log_path, 'w', encoding='utf-8') as log_file:
                while proc and proc.poll() is None:
                    line = proc.stdout.readline()
                    if line:
                        log_file.write(line)
                        log_file.flush()
                remaining = proc.stdout.read()
                if remaining:
                    log_file.write(remaining)
                    log_file.flush()
                retcode = proc.poll()
                log_file.write(f"\n[Process exited with code: {retcode}]\n")
                log_file.flush()
        
        log_thread = threading.Thread(target=write_log, daemon=True)
        log_thread.start()
        
        STREAM_PROCESSES[stream_id] = proc
        return True, f"流 {stream_config.get('name', stream_id)} 已启动 (PID: {proc.pid})"
    except Exception as e:
        return False, f"启动失败: {str(e)}"

def stop_stream_process(stream_id):
    if stream_id not in STREAM_PROCESSES or not STREAM_PROCESSES[stream_id] or STREAM_PROCESSES[stream_id].poll() is not None:
        return False, "该流未运行"
    
    try:
        proc = STREAM_PROCESSES[stream_id]
        if sys.platform == 'win32':
            subprocess.run(['taskkill', '/F', '/T', '/PID', str(proc.pid)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        else:
            proc.terminate()
            proc.wait(timeout=5)
        STREAM_PROCESSES[stream_id] = None
        return True, "流已停止"
    except Exception as e:
        return False, f"停止失败: {str(e)}"

def get_stream_status(stream_id):
    if stream_id in STREAM_PROCESSES and STREAM_PROCESSES[stream_id]:
        poll_result = STREAM_PROCESSES[stream_id].poll()
        if poll_result is None:
            return "running"
        else:
            if stream_id in STREAM_PROCESSES:
                STREAM_PROCESSES[stream_id] = None
    
    log_path = os.path.join(STREAMS_DIR, f"{stream_id}.log")
    if os.path.exists(log_path):
        try:
            with open(log_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            if "[Process exited with code:" in content or "Pushing stopped." in content:
                return "stopped"
            
            if content.strip() and "Pushing started" in content:
                with open(log_path, 'a', encoding='utf-8') as f:
                    f.write("\n[Process exited with code: -1]\n")
                return "stopped"
        except:
            pass
    
    return "stopped"

def get_stream_log(stream_id):
    log_path = os.path.join(STREAMS_DIR, f"{stream_id}.log")
    if os.path.exists(log_path):
        with open(log_path, 'r', encoding='utf-8') as f:
            content = f.read()
            return content[-5000:] if len(content) > 5000 else content
    return ""

def get_stream_hls_url(stream_id):
    stream_config = load_stream(stream_id)
    if not stream_config:
        return ""
    rtsp_url = stream_config.get("push", {}).get("rtsp_url", "")
    if not rtsp_url:
        return ""
    
    from urllib.parse import urlparse
    parsed = urlparse(rtsp_url)
    path = parsed.path.rstrip("/")
    
    if get_server_status() != "running":
        return ""
    
    return f"http://localhost:8888{path}/index.m3u8"

def start_server(server_id):
    global SERVER_PROCESS
    if server_id not in SERVER_CONFIGS:
        return False, f"未知的服务器: {server_id}"
    server_config = SERVER_CONFIGS[server_id]
    server_path = server_config["path"]
    if not os.path.exists(server_path):
        return False, f"服务器程序不存在"
    if SERVER_PROCESS and SERVER_PROCESS.poll() is None:
        return True, "服务器已在运行中"
    try:
        SERVER_PROCESS = subprocess.Popen(
            [server_path], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            cwd=os.path.dirname(server_path),
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == 'win32' else 0
        )
        time.sleep(1)
        if SERVER_PROCESS.poll() is not None:
            return False, "服务器启动后立即退出"
        return True, f"{server_config['name']} 已启动 (PID: {SERVER_PROCESS.pid})"
    except Exception as e:
        return False, f"启动服务器失败: {str(e)}"

def stop_server():
    global SERVER_PROCESS
    if not SERVER_PROCESS or SERVER_PROCESS.poll() is not None:
        return True, "服务器未运行"
    try:
        if sys.platform == 'win32':
            subprocess.run(['taskkill', '/F', '/T', '/PID', str(SERVER_PROCESS.pid)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        else:
            SERVER_PROCESS.terminate()
            SERVER_PROCESS.wait(timeout=5)
        SERVER_PROCESS = None
        return True, "服务器已停止"
    except Exception as e:
        return False, f"停止服务器失败: {str(e)}"

def get_server_status():
    if SERVER_PROCESS and SERVER_PROCESS.poll() is None:
        return "running"
    return "stopped"

def get_available_servers():
    servers = []
    for server_id, server_config in SERVER_CONFIGS.items():
        servers.append({
            "id": server_id,
            "name": server_config["name"],
            "path": server_config["path"],
            "available": os.path.exists(server_config["path"]),
            "rtsp_port": server_config["rtsp_port"],
            "http_port": server_config["http_port"]
        })
    return servers

def get_system_info():
    cpu_percent = psutil.cpu_percent(interval=0.5)
    memory = psutil.virtual_memory()
    disk = psutil.disk_usage('/')
    if sys.platform == 'win32':
        for partition in psutil.disk_partitions():
            if 'C:\\' in partition.mountpoint or partition.mountpoint == '\\':
                disk = psutil.disk_usage(partition.mountpoint)
                break
    gpu_info = get_gpu_info()
    
    running_count = sum(1 for s_id in STREAM_PROCESSES if get_stream_status(s_id) == "running")
    
    process_info = None
    total_cpu = 0
    total_mem = 0
    total_threads = 0
    for s_id, proc in STREAM_PROCESSES.items():
        if proc and proc.poll() is None:
            try:
                p = psutil.Process(proc.pid)
                total_cpu += p.cpu_percent(interval=0.1)
                total_mem += p.memory_info().rss / 1024 / 1024
                total_threads += p.num_threads()
            except:
                pass
    
    if running_count > 0:
        process_info = {
            "cpu_percent": round(total_cpu, 1),
            "memory_mb": round(total_mem, 1),
            "threads": total_threads,
            "running_streams": running_count
        }
    
    uptime = time.time() - SYSTEM_STATS["start_time"]
    
    return {
        "cpu": {"percent": cpu_percent, "cores": psutil.cpu_count(), "physical_cores": psutil.cpu_count(logical=False)},
        "memory": {"total_gb": round(memory.total / 1024 / 1024 / 1024, 2), "used_gb": round(memory.used / 1024 / 1024 / 1024, 2), "percent": memory.percent, "available_gb": round(memory.available / 1024 / 1024 / 1024, 2)},
        "disk": {"total_gb": round(disk.total / 1024 / 1024 / 1024, 2), "used_gb": round(disk.used / 1024 / 1024 / 1024, 2), "percent": disk.percent, "free_gb": round(disk.free / 1024 / 1024 / 1024, 2)},
        "gpu": gpu_info,
        "process": process_info,
        "uptime": round(uptime, 1),
        "platform": {"system": platform.system(), "node": platform.node(), "processor": platform.processor()}
    }

def get_gpu_info():
    gpu_info = {"available": False, "name": "N/A", "memory_used": 0, "memory_total": 0, "utilization": 0}
    try:
        result = subprocess.run(
            ['nvidia-smi', '--query-gpu=name,memory.used,memory.total,utilization.gpu', '--format=csv,noheader,nounits'],
            capture_output=True, text=True, timeout=5,
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
        )
        if result.returncode == 0 and result.stdout.strip():
            parts = result.stdout.strip().split('\n')[0].split(',')
            if len(parts) >= 4:
                gpu_info["available"] = True
                gpu_info["name"] = parts[0].strip()
                gpu_info["memory_used"] = float(parts[1].strip())
                gpu_info["memory_total"] = float(parts[2].strip())
                gpu_info["utilization"] = float(parts[3].strip())
    except:
        pass
    return gpu_info

def get_recordings():
    recordings = []
    rec_dir = os.path.join(PROJECT_ROOT, "recordings")
    if os.path.exists(rec_dir):
        for file in glob.glob(os.path.join(rec_dir, "*.mp4")):
            stat = os.stat(file)
            recordings.append({
                "name": os.path.basename(file),
                "size_mb": round(stat.st_size / 1024 / 1024, 2),
                "created": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(stat.st_ctime)),
                "path": file
            })
    recordings.sort(key=lambda x: x["created"], reverse=True)
    return recordings

# ==================== 路由 ====================

@app.route('/login')
def login_page():
    if 'user' in session:
        return redirect(url_for('index'))
    return render_template('login.html')

@app.route('/api/login', methods=['POST'])
def api_login():
    data = request.json or {}
    username = data.get('username', '').strip()
    password = data.get('password', '')
    if not username or not password:
        return jsonify({"success": False, "message": "用户名和密码不能为空"})
    users = load_users()
    if username in users and users[username]['password'] == hash_password(password):
        session['user'] = username
        session['role'] = users[username].get('role', 'user')
        return jsonify({"success": True, "message": "登录成功", "user": username})
    return jsonify({"success": False, "message": "用户名或密码错误"})

@app.route('/api/register', methods=['POST'])
def api_register():
    data = request.json or {}
    username = data.get('username', '').strip()
    password = data.get('password', '')
    confirm_password = data.get('confirm_password', '')
    if not username or not password:
        return jsonify({"success": False, "message": "用户名和密码不能为空"})
    if len(username) < 3:
        return jsonify({"success": False, "message": "用户名至少3个字符"})
    if len(password) < 3:
        return jsonify({"success": False, "message": "密码至少3个字符"})
    if password != confirm_password:
        return jsonify({"success": False, "message": "两次密码不一致"})
    users = load_users()
    if username in users:
        return jsonify({"success": False, "message": "用户名已存在"})
    users[username] = {"password": hash_password(password), "role": "user"}
    save_users(users)
    return jsonify({"success": True, "message": "注册成功，请登录"})

@app.route('/api/logout', methods=['POST'])
def api_logout():
    session.clear()
    return jsonify({"success": True, "message": "已登出"})

@app.route('/api/check_auth')
def api_check_auth():
    if 'user' in session:
        return jsonify({"logged_in": True, "user": session['user'], "role": session.get('role', 'user')})
    return jsonify({"logged_in": False})

@app.route('/')
@login_required
def index():
    return render_template('index.html')

# ==================== 流管理 API ====================

@app.route('/api/streams', methods=['GET'])
@login_required
def api_streams():
    return jsonify(load_stream_list())

@app.route('/api/streams', methods=['POST'])
@admin_required
def api_create_stream():
    data = request.json
    if not data:
        return jsonify({"success": False, "message": "无效数据"})
    stream_id = data.get("id", "").strip()
    if not stream_id:
        stream_id = "stream_" + uuid.uuid4().hex[:8]
    if load_stream(stream_id):
        return jsonify({"success": False, "message": "流ID已存在"})
    save_stream(stream_id, data)
    return jsonify({"success": True, "message": "流已创建", "id": stream_id})

@app.route('/api/streams/<stream_id>', methods=['GET'])
@login_required
def api_get_stream(stream_id):
    stream = load_stream(stream_id)
    if not stream:
        return jsonify({"success": False, "message": "流不存在"}), 404
    stream["id"] = stream_id
    stream["_status"] = get_stream_status(stream_id)
    stream["_hls_url"] = get_stream_hls_url(stream_id)
    return jsonify(stream)

@app.route('/api/streams/<stream_id>', methods=['PUT'])
@admin_required
def api_update_stream(stream_id):
    data = request.json
    if not data:
        return jsonify({"success": False, "message": "无效数据"})
    if get_stream_status(stream_id) == "running":
        return jsonify({"success": False, "message": "流正在运行中，请先停止再修改"})
    save_stream(stream_id, data)
    return jsonify({"success": True, "message": "流配置已更新"})

@app.route('/api/streams/<stream_id>', methods=['DELETE'])
@admin_required
def api_delete_stream(stream_id):
    if get_stream_status(stream_id) == "running":
        return jsonify({"success": False, "message": "流正在运行中，请先停止再删除"})
    delete_stream_file(stream_id)
    return jsonify({"success": True, "message": "流已删除"})

@app.route('/api/streams/<stream_id>/start', methods=['POST'])
@admin_required
def api_start_stream(stream_id):
    data = request.json or {}
    auto_start_server = data.get('start_server', False)
    if auto_start_server:
        server_status = get_server_status()
        if server_status != "running":
            success, msg = start_server("mediamtx")
            if not success:
                return jsonify({"success": False, "message": f"服务器启动失败: {msg}"})
            time.sleep(2)
    success, message = start_stream_process(stream_id)
    return jsonify({"success": success, "message": message})

@app.route('/api/streams/<stream_id>/stop', methods=['POST'])
@admin_required
def api_stop_stream(stream_id):
    success, message = stop_stream_process(stream_id)
    return jsonify({"success": success, "message": message})

@app.route('/api/streams/<stream_id>/status')
@login_required
def api_stream_status(stream_id):
    return jsonify({
        "id": stream_id,
        "status": get_stream_status(stream_id),
        "hls_url": get_stream_hls_url(stream_id)
    })

@app.route('/api/streams/<stream_id>/log')
@admin_required
def api_stream_log(stream_id):
    return jsonify({"log": get_stream_log(stream_id)})

@app.route('/api/streams/start_all', methods=['POST'])
@admin_required
def api_start_all_streams():
    results = []
    for stream in load_stream_list():
        if stream.get("enabled", True) and get_stream_status(stream["id"]) != "running":
            success, msg = start_stream_process(stream["id"])
            results.append({"id": stream["id"], "success": success, "message": msg})
            if success:
                time.sleep(3)
    return jsonify({"results": results})

@app.route('/api/streams/stop_all', methods=['POST'])
@admin_required
def api_stop_all_streams():
    results = []
    for stream_id in list(STREAM_PROCESSES.keys()):
        if get_stream_status(stream_id) == "running":
            success, msg = stop_stream_process(stream_id)
            results.append({"id": stream_id, "success": success, "message": msg})
    return jsonify({"results": results})

# ==================== 其他 API ====================

@app.route('/api/devices')
@login_required
def api_devices():
    return jsonify(get_video_devices())

@app.route('/api/device_capabilities/<path:device_name>')
@login_required
def api_device_capabilities(device_name):
    return jsonify(get_device_capabilities(device_name))

@app.route('/api/models')
@login_required
def api_models():
    return jsonify(get_available_models())

COCO_CLASSES = [
    {"id": 0, "name": "person", "label": "人"},
    {"id": 1, "name": "bicycle", "label": "自行车"},
    {"id": 2, "name": "car", "label": "汽车"},
    {"id": 3, "name": "motorcycle", "label": "摩托车"},
    {"id": 4, "name": "airplane", "label": "飞机"},
    {"id": 5, "name": "bus", "label": "公交车"},
    {"id": 6, "name": "train", "label": "火车"},
    {"id": 7, "name": "truck", "label": "卡车"},
    {"id": 8, "name": "boat", "label": "船"},
    {"id": 9, "name": "traffic light", "label": "红绿灯"},
    {"id": 10, "name": "fire hydrant", "label": "消防栓"},
    {"id": 11, "name": "stop sign", "label": "停车标志"},
    {"id": 13, "name": "bench", "label": "长椅"},
    {"id": 14, "name": "bird", "label": "鸟"},
    {"id": 15, "name": "cat", "label": "猫"},
    {"id": 16, "name": "dog", "label": "狗"},
    {"id": 17, "name": "horse", "label": "马"},
    {"id": 18, "name": "sheep", "label": "羊"},
    {"id": 19, "name": "cow", "label": "牛"},
    {"id": 20, "name": "elephant", "label": "大象"},
    {"id": 21, "name": "bear", "label": "熊"},
    {"id": 22, "name": "zebra", "label": "斑马"},
    {"id": 23, "name": "giraffe", "label": "长颈鹿"},
    {"id": 24, "name": "backpack", "label": "背包"},
    {"id": 25, "name": "umbrella", "label": "雨伞"},
    {"id": 27, "name": "handbag", "label": "手提包"},
    {"id": 28, "name": "tie", "label": "领带"},
    {"id": 31, "name": "skis", "label": "滑雪板"},
    {"id": 32, "name": "snowboard", "label": "单板"},
    {"id": 33, "name": "sports ball", "label": "运动球"},
    {"id": 34, "name": "kite", "label": "风筝"},
    {"id": 35, "name": "baseball bat", "label": "棒球棒"},
    {"id": 36, "name": "baseball glove", "label": "棒球手套"},
    {"id": 37, "name": "skateboard", "label": "滑板"},
    {"id": 38, "name": "surfboard", "label": "冲浪板"},
    {"id": 39, "name": "tennis racket", "label": "网球拍"},
    {"id": 40, "name": "bottle", "label": "瓶子"},
    {"id": 41, "name": "wine glass", "label": "酒杯"},
    {"id": 42, "name": "cup", "label": "杯子"},
    {"id": 43, "name": "fork", "label": "叉子"},
    {"id": 44, "name": "knife", "label": "刀"},
    {"id": 45, "name": "spoon", "label": "勺子"},
    {"id": 46, "name": "bowl", "label": "碗"},
    {"id": 47, "name": "banana", "label": "香蕉"},
    {"id": 48, "name": "apple", "label": "苹果"},
    {"id": 49, "name": "sandwich", "label": "三明治"},
    {"id": 50, "name": "orange", "label": "橙子"},
    {"id": 51, "name": "broccoli", "label": "西兰花"},
    {"id": 52, "name": "carrot", "label": "胡萝卜"},
    {"id": 53, "name": "hot dog", "label": "热狗"},
    {"id": 54, "name": "pizza", "label": "披萨"},
    {"id": 55, "name": "donut", "label": "甜甜圈"},
    {"id": 56, "name": "cake", "label": "蛋糕"},
    {"id": 57, "name": "chair", "label": "椅子"},
    {"id": 58, "name": "couch", "label": "沙发"},
    {"id": 59, "name": "potted plant", "label": "盆栽"},
    {"id": 60, "name": "bed", "label": "床"},
    {"id": 61, "name": "dining table", "label": "餐桌"},
    {"id": 62, "name": "toilet", "label": "马桶"},
    {"id": 63, "name": "tv", "label": "电视"},
    {"id": 64, "name": "laptop", "label": "笔记本"},
    {"id": 65, "name": "mouse", "label": "鼠标"},
    {"id": 66, "name": "remote", "label": "遥控器"},
    {"id": 67, "name": "keyboard", "label": "键盘"},
    {"id": 68, "name": "cell phone", "label": "手机"},
    {"id": 69, "name": "microwave", "label": "微波炉"},
    {"id": 70, "name": "oven", "label": "烤箱"},
    {"id": 71, "name": "toaster", "label": "烤面包机"},
    {"id": 72, "name": "sink", "label": "水槽"},
    {"id": 73, "name": "refrigerator", "label": "冰箱"},
    {"id": 74, "name": "book", "label": "书"},
    {"id": 75, "name": "clock", "label": "时钟"},
    {"id": 76, "name": "vase", "label": "花瓶"},
    {"id": 77, "name": "scissors", "label": "剪刀"},
    {"id": 78, "name": "teddy bear", "label": "泰迪熊"},
    {"id": 79, "name": "toothbrush", "label": "牙刷"}
]

@app.route('/api/coco_classes')
@login_required
def api_coco_classes():
    return jsonify(COCO_CLASSES)

@app.route('/api/server/start', methods=['POST'])
@admin_required
def api_server_start():
    data = request.json or {}
    server_id = data.get('server_id', 'mediamtx')
    success, message = start_server(server_id)
    return jsonify({"success": success, "message": message})

@app.route('/api/server/stop', methods=['POST'])
@admin_required
def api_server_stop():
    success, message = stop_server()
    return jsonify({"success": success, "message": message})

@app.route('/api/server/status')
@login_required
def api_server_status():
    return jsonify({"status": get_server_status(), "servers": get_available_servers()})

@app.route('/api/servers')
@login_required
def api_servers():
    return jsonify(get_available_servers())

@app.route('/api/system')
@admin_required
def api_system():
    return jsonify(get_system_info())

@app.route('/api/recordings')
@login_required
def api_recordings():
    return jsonify(get_recordings())

@app.route('/api/recording/play', methods=['POST'])
@login_required
def api_recording_play():
    data = request.json or {}
    filename = data.get('filename', '')
    if not filename:
        return jsonify({"success": False, "message": "文件名不能为空"})
    file_path = os.path.join(PROJECT_ROOT, "recordings", filename)
    if not os.path.exists(file_path):
        return jsonify({"success": False, "message": "文件不存在"})
    try:
        if sys.platform == 'win32':
            os.startfile(file_path)
        elif sys.platform == 'darwin':
            subprocess.Popen(['open', file_path])
        else:
            subprocess.Popen(['xdg-open', file_path])
        return jsonify({"success": True, "message": f"正在播放: {filename}"})
    except Exception as e:
        return jsonify({"success": False, "message": f"播放失败: {str(e)}"})

@app.route('/api/recording/delete', methods=['POST'])
@admin_required
def api_recording_delete():
    data = request.json or {}
    filename = data.get('filename', '')
    if not filename:
        return jsonify({"success": False, "message": "文件名不能为空"})
    file_path = os.path.join(PROJECT_ROOT, "recordings", filename)
    if not os.path.exists(file_path):
        return jsonify({"success": False, "message": "文件不存在"})
    try:
        os.remove(file_path)
        return jsonify({"success": True, "message": f"已删除: {filename}"})
    except Exception as e:
        return jsonify({"success": False, "message": f"删除失败: {str(e)}"})

if __name__ == '__main__':
    ensure_streams_dir()
    print("=" * 50)
    print("智能视频分析系统")
    print("=" * 50)
    print(f"项目根目录: {PROJECT_ROOT}")
    print(f"流配置目录: {STREAMS_DIR}")
    print(f"可执行文件: {get_executable_path()}")
    print("检测到的视频设备:")
    for device in get_video_devices():
        print(f"  - {device}")
    print("可用模型:")
    for model in get_available_models():
        print(f"  - {model['name']}")
    print("前端地址: http://localhost:5000")
    print("=" * 50)
    app.run(host='0.0.0.0', port=5000, debug=True, threaded=True)
