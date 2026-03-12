# Video Push Manager
# 运行此文件启动Web管理界面
# 前端地址: http://localhost:5000

from flask import Flask, render_template, request, jsonify
import os
import json
import subprocess
import threading
import glob
import sys
import re

app = Flask(__name__)

# 获取项目根目录
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG_FILE = os.path.join(PROJECT_ROOT, "config.json")
PROCESS = None
LOG_FILE = os.path.join(PROJECT_ROOT, "push_manager.log")

def get_video_devices():
    """获取可用视频设备"""
    devices = []
    
    try:
        # 使用 ffmpeg 列出 dshow 设备
        result = subprocess.run(
            ["ffmpeg", "-list_devices", "true", "-f", "dshow", "-i", "dummy"],
            capture_output=True,
            text=True,
            timeout=10,
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
        )
        
        # ffmpeg 输出在 stderr 中
        output = result.stderr
        print(f"FFmpeg output:\n{output}")
        
        lines = output.split('\n')
        
        for line in lines:
            # 匹配格式: [dshow @ ...]  "Device Name" (video)
            if '(video)' in line:
                match = re.search(r'"([^"]+)"', line)
                if match:
                    device_name = match.group(1).strip()
                    # 排除 @device 备注和重复项
                    if device_name and '@' not in device_name and device_name not in devices:
                        devices.append(device_name)
                        print(f"Found device: {device_name}")
                        
    except subprocess.TimeoutExpired:
        print("获取设备超时")
    except FileNotFoundError:
        print("未找到 ffmpeg，请确保 ffmpeg 在 PATH 中")
    except Exception as e:
        print(f"获取设备失败: {e}")
        import traceback
        traceback.print_exc()
    
    return devices

def load_config():
    """加载配置文件"""
    if os.path.exists(CONFIG_FILE):
        with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
            return json.load(f)
    return {
        "push": {
            "rtsp_url": "rtsp://192.168.1.1:554/live/test",
            "video_width": 1920,
            "video_height": 1080,
            "video_bitrate": 6000000,
            "frame_rate": 24
        },
        "capture": {
            "device_name": "",
            "input_format": "dshow",
            "video_width": 640,
            "video_height": 360,
            "framerate": 24
        },
        "detector": {
            "enabled": True,
            "model_path": "model/yolo11n.onnx",
            "confidence_threshold": 0.5,
            "input_width": 640,
            "input_height": 640,
            "classes": ["person"]
        },
        "recorder": {
            "enabled": True,
            "output_dir": "recordings",
            "person_leave_timeout_ms": 3000
        },
        "log": {
            "level": "info",
            "debug_enabled": False
        }
    }

def save_config_file(config):
    """保存配置文件"""
    with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
        json.dump(config, f, indent=4, ensure_ascii=False)
    return True

def get_executable_path():
    """获取可执行文件路径"""
    search_paths = [
        os.path.join(PROJECT_ROOT, "build", "bin", "Debug", "VideoPush.exe"),
        os.path.join(PROJECT_ROOT, "build", "bin", "Release", "VideoPush.exe"),
        os.path.join(PROJECT_ROOT, "VideoPush.exe")
    ]
    
    for path in search_paths:
        if os.path.exists(path):
            return path
    return None

def start_push_process():
    """启动推流进程"""
    global PROCESS
    
    if PROCESS and PROCESS.poll() is None:
        return False, "进程已在运行中"
    
    exe_path = get_executable_path()
    if not exe_path:
        return False, "未找到可执行文件"
    
    try:
        PROCESS = subprocess.Popen(
            [exe_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            cwd=PROJECT_ROOT,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == 'win32' else 0
        )
        
        def write_log():
            with open(LOG_FILE, 'w', encoding='utf-8') as log_file:
                while PROCESS and PROCESS.poll() is None:
                    line = PROCESS.stdout.readline()
                    if line:
                        log_file.write(line)
                        log_file.flush()
        
        log_thread = threading.Thread(target=write_log, daemon=True)
        log_thread.start()
        
        return True, f"进程已启动: {exe_path}"
    except Exception as e:
        return False, f"启动失败: {str(e)}"

def stop_push_process():
    """停止推流进程"""
    global PROCESS
    
    if not PROCESS or PROCESS.poll() is not None:
        return False, "进程未运行"
    
    try:
        if sys.platform == 'win32':
            subprocess.run(['taskkill', '/F', '/T', '/PID', str(PROCESS.pid)], 
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        else:
            PROCESS.terminate()
            PROCESS.wait(timeout=5)
        
        PROCESS = None
        return True, "进程已停止"
    except Exception as e:
        return False, f"停止失败: {str(e)}"

def get_process_status():
    """获取进程状态"""
    if PROCESS and PROCESS.poll() is None:
        return "running"
    return "stopped"

def get_log_content():
    """获取日志内容"""
    if os.path.exists(LOG_FILE):
        with open(LOG_FILE, 'r', encoding='utf-8') as f:
            content = f.read()
            if len(content) > 5000:
                content = content[-5000:]
            return content
    return ""

# ==================== 路由 ====================

@app.route('/')
def index():
    """主页面"""
    config = load_config()
    devices = get_video_devices()
    exe_path = get_executable_path()
    status = get_process_status()
    
    return render_template('index.html', 
                         config=config,
                         devices=devices,
                         exe_path=exe_path,
                         status=status)

@app.route('/api/config', methods=['GET', 'POST'])
def api_config():
    """配置API"""
    if request.method == 'GET':
        return jsonify(load_config())
    else:
        config = request.json
        save_config_file(config)
        return jsonify({"success": True, "message": "配置已保存"})

@app.route('/api/devices')
def api_devices():
    """获取设备列表"""
    devices = get_video_devices()
    print(f"API returning devices: {devices}")
    return jsonify(devices)

@app.route('/api/start', methods=['POST'])
def api_start():
    """启动推流"""
    success, message = start_push_process()
    return jsonify({"success": success, "message": message})

@app.route('/api/stop', methods=['POST'])
def api_stop():
    """停止推流"""
    success, message = stop_push_process()
    return jsonify({"success": success, "message": message})

@app.route('/api/status')
def api_status():
    """获取状态"""
    return jsonify({
        "status": get_process_status(),
        "exe_path": get_executable_path()
    })

@app.route('/api/log')
def api_log():
    """获取日志"""
    return jsonify({"log": get_log_content()})

if __name__ == '__main__':
    print("=" * 50)
    print("视频推流管理系统")
    print("=" * 50)
    print(f"项目根目录: {PROJECT_ROOT}")
    print(f"配置文件: {CONFIG_FILE}")
    print(f"可执行文件: {get_executable_path()}")
    
    # 测试获取设备
    print("检测到的视频设备:")
    for device in get_video_devices():
        print(f"  - {device}")
    
    print("前端地址: http://localhost:5000")
    print("=" * 50)
    
    app.run(host='0.0.0.0', port=5000, debug=True)
