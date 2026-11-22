function Update-MetricHistory($ch, [string]$date) {
    $specs=@(@('kwh','Энергия','кВт·ч'),@('p','Средняя мощность','Вт'),@('pmax','Пиковая мощность','Вт'),@('v','Напряжение','В'),@('t','Температура реле','°C'))
    if (-not $script:metricHidden) { $script:metricHidden = @{} }
    if($ch.AccessibleDescription -ne 'metric-history') {
        $ch.Series.Clear();$ch.ChartAreas.Clear();$ch.Legends.Clear();$ch.Titles.Clear()
        $i=0
        foreach($spec in $specs){
            if($i -eq 0){
            $a=New-Object System.Windows.Forms.DataVisualization.Charting.ChartArea 'combined'
            $a.BackColor='Transparent';$a.Position.Auto=$false
            $a.Position.X=0;$a.Position.Width=98;$a.Position.Y=14;$a.Position.Height=78
            $a.AxisX.Minimum=-0.5;$a.AxisX.Maximum=23.5;$a.AxisX.Interval=3;$a.AxisX.IntervalOffset=0.5
            $a.AxisX.MajorGrid.Enabled=$false;$a.AxisY.MajorGrid.LineColor=C $script:CLR.line
            $a.AxisX.LabelStyle.ForeColor=C $script:CLR.dim;$a.AxisX.LabelStyle.Enabled=$true;$a.AxisX.LabelStyle.Format='00';$a.AxisX.IsLabelAutoFit=$false;$a.AxisX.LabelStyle.Font=Fnt 'Segoe UI' 8 $false;$a.AxisY.LabelStyle.ForeColor=C $script:CLR.dim
            $a.AxisY.Title='% от максимума каждого показателя';$a.AxisY.TitleForeColor=C $script:CLR.text
            $a.AxisY.Minimum=0;$a.AxisY.Maximum=110;$a.AxisY.LabelStyle.Format='0'
            $ch.ChartAreas.Add($a)
            $legend=New-Object System.Windows.Forms.DataVisualization.Charting.Legend 'metrics';$legend.Docking='Top';$legend.BackColor='Transparent';$legend.ForeColor=C $script:CLR.text;$ch.Legends.Add($legend)
            }
            $s=New-Object System.Windows.Forms.DataVisualization.Charting.Series $spec[0]
            $palette=@('#24C7E8','#F3AD33','#ED6B54','#80AAFF','#EF81BD')
            $s.ChartArea='combined';$s.ChartType='Column';$s.Color=C $palette[$i];$s.Legend='metrics';$s.LegendText=$spec[1]+' ('+$spec[2]+')';$s.LegendToolTip='Нажмите, чтобы показать или скрыть показатель'
            $ch.Series.Add($s);$i++
        }
        $ch.AccessibleDescription='metric-history';$ch.Tag=$null
    }
    if($ch.AccessibleName -ne $date){$ch.Tag=$null;$ch.AccessibleName=$date}
    $map=@{}
    if($script:histCache.ContainsKey($date)){foreach($r in $script:histCache[$date]){$map[[int]$r.h]=$r}}
    foreach($spec in $specs){
        $s=$ch.Series[$spec[0]];$s.Points.Clear()
        $s.LegendText=$spec[1]+' ('+$spec[2]+')'+$(if($script:metricHidden[$spec[0]]){' — скрыто'}else{''})
        $max=0.0;foreach($record in $map.Values){$v=$record.($spec[0]);if([int]$record.n -gt 0 -and $null -ne $v){$max=[Math]::Max($max,[Math]::Abs([double]$v))}};if($max -eq 0){$max=1}
        for($hour=0;$hour -lt 24;$hour++){
            $r=$map[$hour];$value=if($r){$r.($spec[0])}else{$null}
            $idx=$s.Points.AddXY($hour,$(if($null -ne $value){[Math]::Abs([double]$value)*100/$max}else{0}))
            $p=$s.Points[$idx]
            $p.ToolTip=if($null -ne $value){('{0:00}:00 · {1}: {2} {3}' -f $hour,$spec[1],$value,$spec[2])}else{'нет данных'}
            $p.IsEmpty=($null -eq $r -or [int]$r.n -le 0 -or $null -eq $value)
            if($script:metricHidden[$spec[0]]) { $p.Color=[System.Drawing.Color]::Transparent; $p.BorderColor=[System.Drawing.Color]::Transparent }
            elseif($null -ne $ch.Tag -and $hour -eq [int]$ch.Tag){$p.BorderWidth=2;$p.BorderColor=C $script:CLR.text}
        }
    }
    $ch.Titles.Clear();$caption=$date+' · выберите час'
    if($null -ne $ch.Tag){
        $hour=[int]$ch.Tag;$r=$map[$hour];$caption=$date+(' · {0:00}:00–{1:00}:00' -f $hour,($hour+1))
        if($r -and [int]$r.n -gt 0){$caption+=(' · {0} кВт·ч · {1} Вт · пик {2} Вт · {3} В · {4} °C' -f (Num $r.kwh '0.000'),(Num $r.p '0'),(Num $r.pmax '0'),(Num $r.v '0.0'),(Num $r.t '0.0'))}else{$caption+=' · нет данных'}
    }
    $title=New-Object System.Windows.Forms.DataVisualization.Charting.Title
    if($null -ne $script:lblMetricSelected){$script:lblMetricSelected.Text=$caption;$title.Text='Нажмите на легенду, чтобы скрыть показатель'}else{$title.Text=$caption}
    $title.ForeColor=C $script:CLR.text;$title.Font=Fnt 'Segoe UI' 9 $true
    $ch.Titles.Add($title)
}
