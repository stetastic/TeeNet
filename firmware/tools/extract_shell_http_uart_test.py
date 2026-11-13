"""Exercise the actual private HTTP/normal smoothing selection in UART replies."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'main/app.c').read_text(encoding='utf-8')
start = source.index('        control_report(&settings,actual_a,request,house_power_w,house_ok,currents);')
end = source.index('        bool holding=wallbox_meter_hold_locked(report_now);', start)
(root / 'tests/shell_http_uart_actual.inc').write_text(source[start:end], encoding='utf-8')
start=source.index('                    /* PRIVATE_HTTP_CURRENT_SENT_BEGIN */')
end=source.index('                    /* PRIVATE_HTTP_CURRENT_SENT_END */',start)
(root/'tests/shell_http_uart_sent_actual.inc').write_text(source[start:end],encoding='utf-8')
