$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Windows.Forms.DataVisualization
$e=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Resolve-Path pc/DomMonitor.ps1),[ref]$null,[ref]$e)
if($e){throw ($e|Out-String)}
foreach($name in @('Update-EnergyChart','Field','Num','Get-ScreenChartHour')){$n=$ast.Find({param($x)$x -is [Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq $name},$true);Invoke-Expression $n.Extent.Text}
. (Join-Path $PWD pc/MetricHistory.ps1)
function C($v){if(-not$v){$v='#FFFFFF'};[Drawing.ColorTranslator]::FromHtml($v)}
function Fnt($name,$size,$bold){New-Object Drawing.Font($name,$size)}
function Add-Log{param($text,$kind)}
function Write-AppLog{param($text);throw $text}
function Request-Day{param($date)}
function Show-Page{param($page);$script:lastPage=$page}
function Update-HistoryView{}
$script:CLR=@{line='#334155';text='#E2E8F0';dim='#94A3B8';warn='#FBBF24'}
$date=(Get-Date).ToString('yyyy-MM-dd');$script:histSel=$date;$script:chartMode='day';$script:histAt=@{};$script:histCache=@{}
$script:histCache[$date]=@(0..23|ForEach-Object{[pscustomobject]@{h=$_;n=120;kwh=$_/100;p=$_*10;pmax=$_*20;v=235+$_/10;t=19+$_/10}})
$script:lblMetricSelected=New-Object Windows.Forms.Label
$form=New-Object Windows.Forms.Form;$form.StartPosition='Manual';$form.Location=New-Object Drawing.Point(-20000,-20000);$form.Size=New-Object Drawing.Size(1100,760)
$form.BackColor=C '#101224'
$chartScreen=New-Object Windows.Forms.DataVisualization.Charting.Chart;$chartScreen.Dock='Top';$chartScreen.Height=300
$chartScreen.Legends.Add((New-Object Windows.Forms.DataVisualization.Charting.Legend 'Default'))
$chartScreen.BackColor=C '#101224';$chartScreen.Legends[0].BackColor=[Drawing.Color]::Transparent;$chartScreen.Legends[0].ForeColor=C $script:CLR.text
foreach($name in @('main','temp')){$chartScreen.ChartAreas.Add((New-Object Windows.Forms.DataVisualization.Charting.ChartArea $name))}
foreach($name in @('Энергия, Вт·ч','День, Вт·ч','Ночь, Вт·ч','Напряжение, В','Температура, °C','Вчера, В')){$s=New-Object Windows.Forms.DataVisualization.Charting.Series $name;$s.ChartArea='main';$s.ChartType=if($name -like '*Вт·ч'){'Column'}else{'Line'};$chartScreen.Series.Add($s)}
$chartHist=New-Object Windows.Forms.DataVisualization.Charting.Chart;$chartHist.Dock='Bottom';$chartHist.Height=400
$chartHist.BackColor=C '#101224'
$form.Controls.Add($chartScreen);$form.Controls.Add($chartHist)
Update-EnergyChart $chartScreen $date;Update-MetricHistory $chartHist $date
try{
    $form.Show();[Windows.Forms.Application]::DoEvents();$chartScreen.Refresh();$chartHist.Refresh();[Windows.Forms.Application]::DoEvents()
    $layout=New-Object Drawing.Bitmap($form.ClientSize.Width,$form.ClientSize.Height)
    $form.DrawToBitmap($layout,(New-Object Drawing.Rectangle(0,0,$layout.Width,$layout.Height)));$layout.Dispose()
    $node=$ast.Find({param($x)$x -is [Management.Automation.Language.InvokeMemberExpressionAst] -and $x.Extent.Text.StartsWith('$chartScreen.Add_MouseDown(')},$true)
    $handler=$node.Arguments[0].ScriptBlock.GetScriptBlock()
    foreach($hour in 0..23){
        $area=$chartScreen.ChartAreas['main'];$x=[int]$area.AxisX.ValueToPixelPosition($hour+.2);$y=[int]$area.AxisY.ValueToPixelPosition(70)
        $hit=$chartScreen.HitTest($x,$y)
        Write-Output ('hour {0}: hit {1}, area {2}' -f $hour,$hit.ChartElementType,$hit.ChartArea.Name)
        & $handler $chartScreen (New-Object Windows.Forms.MouseEventArgs([Windows.Forms.MouseButtons]::Left,1,$x,$y,0))
        if($script:lastPage -ne 'hist' -or $chartHist.Tag -ne $hour -or $script:lblMetricSelected.Text -notmatch ('{0:00}:00' -f $hour)){throw ('Main chart did not open hour '+$hour)}
    }
    $node=$ast.Find({param($x)$x -is [Management.Automation.Language.InvokeMemberExpressionAst] -and $x.Extent.Text.StartsWith('$chartHist.Add_MouseDown(')},$true)
    $handler=$node.Arguments[0].ScriptBlock.GetScriptBlock();$chartHist.Refresh();[Windows.Forms.Application]::DoEvents()
    foreach($hour in 0..23){
        $area=$chartHist.ChartAreas['combined'];$x=[int]$area.AxisX.ValueToPixelPosition($hour);$y=[int]$area.AxisY.ValueToPixelPosition(40)
        & $handler $chartHist (New-Object Windows.Forms.MouseEventArgs([Windows.Forms.MouseButtons]::Left,1,$x,$y,0))
        if($chartHist.Tag -ne $hour -or $script:lblMetricSelected.Text -notmatch ('{0:00}:00' -f $hour)){throw ('History chart did not select hour '+$hour)}
    }
    $bitmap=New-Object Drawing.Bitmap($form.ClientSize.Width,$form.ClientSize.Height)
    $form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$bitmap.Width,$bitmap.Height)))
    $bitmap.Save((Join-Path $PWD pc/firmware-archive/final-validation-20261113/native-clicks.png));$bitmap.Dispose()
    'PASS: real WinForms HitTest + original MouseDown handlers: all 24 hours on main and history charts'
}finally{$form.Close();$form.Dispose()}
