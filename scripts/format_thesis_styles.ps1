param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [string]$OutputPath,

    [switch]$InPlace,

    [switch]$Visible
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-ComBool {
    param([object]$Value)
    if ([bool]$Value) { return -1 }
    return 0
}

function Get-AlignmentValue {
    param([object]$Value)
    if ($Value -is [int]) { return $Value }
    switch -Regex ([string]$Value) {
        '^Center$' { return 1 }
        '^Right$' { return 2 }
        '^Justify$' { return 3 }
        '^Distributed$' { return 4 }
        default { return 0 }
    }
}

$resolvedInput = (Resolve-Path -LiteralPath $InputPath).Path
if (-not $InPlace -and -not $OutputPath) {
    $directory = Split-Path -Parent $resolvedInput
    $leaf = Split-Path -LeafBase $resolvedInput
    $extension = [System.IO.Path]::GetExtension($resolvedInput)
    $OutputPath = Join-Path $directory "$leaf.formatted$extension"
}

$word = $null
$document = $null
$stats = @{
    Heading1 = 0
    Heading2 = 0
    Heading3 = 0
    Body = 0
    SpecialTitle = 0
}

try {
    $word = New-Object -ComObject Word.Application
    $word.Visible = [bool]$Visible
    $word.DisplayAlerts = 0

    $document = $word.Documents.Open($resolvedInput, $false, $false)

    $heading1Style = $null
    $heading2Style = $null
    $heading3Style = $null
    $bodyStyle = $null

    try { $heading1Style = $document.Styles.Item("Heading 1") } catch { }
    try { $heading2Style = $document.Styles.Item("Heading 2") } catch { }
    try { $heading3Style = $document.Styles.Item("Heading 3") } catch { }
    try { $bodyStyle = $document.Styles.Item("Body Text") } catch { }

    if (-not $heading1Style) {
        $heading1Style = $document.Styles.Add("Heading 1", 1)
    }
    if (-not $heading2Style) {
        $heading2Style = $document.Styles.Add("Heading 2", 1)
    }
    if (-not $heading3Style) {
        $heading3Style = $document.Styles.Add("Heading 3", 1)
    }
    if (-not $bodyStyle) {
        $bodyStyle = $document.Styles.Add("Body Text", 1)
    }

    $heading1Style.Font.Name = "Times New Roman"
    $heading1Style.Font.NameFarEast = "SimHei"
    $heading1Style.Font.Size = 15
    $heading1Style.Font.Bold = -1
    $heading1Style.ParagraphFormat.Alignment = 1
    $heading1Style.ParagraphFormat.FirstLineIndent = 0
    $heading1Style.ParagraphFormat.LeftIndent = 0
    $heading1Style.ParagraphFormat.RightIndent = 0
    $heading1Style.ParagraphFormat.SpaceBefore = 40
    $heading1Style.ParagraphFormat.SpaceAfter = 20
    $heading1Style.ParagraphFormat.LineSpacingRule = 4
    $heading1Style.ParagraphFormat.LineSpacing = 20
    $heading1Style.ParagraphFormat.KeepTogether = -1
    $heading1Style.ParagraphFormat.KeepWithNext = -1
    $heading1Style.ParagraphFormat.OutlineLevel = 1

    $heading2Style.Font.Name = "Times New Roman"
    $heading2Style.Font.NameFarEast = "SimHei"
    $heading2Style.Font.Size = 14
    $heading2Style.Font.Bold = -1
    $heading2Style.ParagraphFormat.Alignment = 0
    $heading2Style.ParagraphFormat.FirstLineIndent = 0
    $heading2Style.ParagraphFormat.LeftIndent = 0
    $heading2Style.ParagraphFormat.RightIndent = 0
    $heading2Style.ParagraphFormat.SpaceBefore = 24
    $heading2Style.ParagraphFormat.SpaceAfter = 6
    $heading2Style.ParagraphFormat.LineSpacingRule = 4
    $heading2Style.ParagraphFormat.LineSpacing = 20
    $heading2Style.ParagraphFormat.KeepTogether = -1
    $heading2Style.ParagraphFormat.KeepWithNext = -1
    $heading2Style.ParagraphFormat.OutlineLevel = 2

    $heading3Style.Font.Name = "Times New Roman"
    $heading3Style.Font.NameFarEast = "SimHei"
    $heading3Style.Font.Size = 13
    $heading3Style.Font.Bold = -1
    $heading3Style.ParagraphFormat.Alignment = 0
    $heading3Style.ParagraphFormat.FirstLineIndent = 0
    $heading3Style.ParagraphFormat.LeftIndent = 0
    $heading3Style.ParagraphFormat.RightIndent = 0
    $heading3Style.ParagraphFormat.SpaceBefore = 12
    $heading3Style.ParagraphFormat.SpaceAfter = 6
    $heading3Style.ParagraphFormat.LineSpacingRule = 4
    $heading3Style.ParagraphFormat.LineSpacing = 20
    $heading3Style.ParagraphFormat.KeepTogether = -1
    $heading3Style.ParagraphFormat.KeepWithNext = -1
    $heading3Style.ParagraphFormat.OutlineLevel = 3

    $bodyStyle.Font.Name = "Times New Roman"
    $bodyStyle.Font.NameFarEast = "SimSun"
    $bodyStyle.Font.Size = 12
    $bodyStyle.Font.Bold = 0
    $bodyStyle.ParagraphFormat.Alignment = 3
    $bodyStyle.ParagraphFormat.FirstLineIndent = $word.CentimetersToPoints(0.74)
    $bodyStyle.ParagraphFormat.LeftIndent = 0
    $bodyStyle.ParagraphFormat.RightIndent = 0
    $bodyStyle.ParagraphFormat.SpaceBefore = 0
    $bodyStyle.ParagraphFormat.SpaceAfter = 0
    $bodyStyle.ParagraphFormat.LineSpacingRule = 4
    $bodyStyle.ParagraphFormat.LineSpacing = 20
    $bodyStyle.ParagraphFormat.OutlineLevel = 10

    $heading1Pattern = '^(\d+)\s+(.+)$'
    $heading2Pattern = '^(\d+\.\d+)\s+(.+)$'
    $heading3Pattern = '^(\d+\.\d+\.\d+)\s+(.+)$'

    $specialTitles = @(
        "\u6458\u8981",
        "\u76EE\u5F55",
        "\u53C2\u8003\u6587\u732E",
        "\u81F4\u8C22",
        "\u58F0\u660E",
        "\u9644\u5F55",
        "Abstract"
    )

    foreach ($paragraph in $document.Paragraphs) {
        $text = ($paragraph.Range.Text -replace "[`r`n]+$", "").Trim()
        
        if ([string]::IsNullOrWhiteSpace($text)) {
            continue
        }

        $kind = $null

        if ($text -match $heading3Pattern) {
            $kind = "Heading3"
        }
        elseif ($text -match $heading2Pattern) {
            $kind = "Heading2"
        }
        elseif ($text -match $heading1Pattern) {
            $kind = "Heading1"
        }
        else {
            $isSpecialTitle = $false
            foreach ($title in $specialTitles) {
                if ($text -eq $title) {
                    $isSpecialTitle = $true
                    break
                }
            }
            if ($isSpecialTitle) {
                $kind = "Heading1"
                $stats.SpecialTitle++
            }
        }

        if ($kind -eq "Heading1") {
            $paragraph.Range.Style = $heading1Style
            $stats.Heading1++
        }
        elseif ($kind -eq "Heading2") {
            $paragraph.Range.Style = $heading2Style
            $stats.Heading2++
        }
        elseif ($kind -eq "Heading3") {
            $paragraph.Range.Style = $heading3Style
            $stats.Heading3++
        }
        else {
            $currentStyleName = ""
            try { $currentStyleName = [string]$paragraph.Range.Style.NameLocal } catch { }
            
            if ($currentStyleName -match "(Heading|标题)" -and $currentStyleName -notmatch "(Body|正文)") {
                continue
            }
            
            $paragraph.Range.Style = $bodyStyle
            $stats.Body++
        }
    }

    if ($InPlace) {
        $document.Save()
        $finalPath = $resolvedInput
    }
    else {
        $resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
        $document.SaveAs([ref]$resolvedOutput)
        $finalPath = $resolvedOutput
    }

    $result = @{
        input = $resolvedInput
        output = $finalPath
        stats = $stats
        message = "Formatting completed"
    }
    $result | ConvertTo-Json -Depth 3
}
finally {
    if ($document) {
        $document.Close([ref]0)
    }

    if ($word) {
        $word.Quit()
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($word)
    }

    [gc]::Collect()
    [gc]::WaitForPendingFinalizers()
}
