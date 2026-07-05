$msgeq = Get-Content "PGA/msgeq7.h"
$insertAfter = '  memcpy(_smooth_l, new_sl, sizeof(_smooth_l));'
$idx = [array]::IndexOf($msgeq, $insertAfter)
Write-Host "msgeq7.h insert point: index $idx"

$newMsgeq = @()
for ($i = 0; $i -lt $msgeq.Length; $i++) {
    $newMsgeq += $msgeq[$i]
    if ($i -eq $idx) {
        $newMsgeq += '  // ?z?w???: ?? 50 ?? (~2.5s) ???????????????'
        $newMsgeq += '  static int _diag_cnt = 0;'
        $newMsgeq += '  _diag_cnt++;'
        $newMsgeq += '  if (_diag_cnt >= 50) {'
        $newMsgeq += '    _diag_cnt = 0;'
        $newMsgeq += '    ESP_LOGI("msgeq7", "L[%d/%d/%d/%d/%d/%d/%d] R[%d/%d/%d/%d/%d/%d/%d] pk[%d] off_r0=%d adc=%d init=%d",'
        $newMsgeq += '      new_l[0], new_l[1], new_l[2], new_l[3], new_l[4], new_l[5], new_l[6],'
        $newMsgeq += '      new_r[0], new_r[1], new_r[2], new_r[3], new_r[4], new_r[5], new_r[6],'
        $newMsgeq += '      new_peak[0], _r_offset[0], _adc_ok, _initialized);'
        $newMsgeq += '  }'
    }
}
$newMsgeq | Set-Content "PGA/msgeq7.h"
Write-Host "msgeq7.h done"
