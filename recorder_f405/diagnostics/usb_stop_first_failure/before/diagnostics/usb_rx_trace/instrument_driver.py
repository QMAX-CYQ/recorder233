"""一次性加入只读诊断钩子；对照 before 备份检查匹配，避免重复插入。"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]

def region(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return start, end, text[start:end]

def change_function(text, signature, transform):
    start, end, body = region(text, signature)
    return text[:start] + transform(body) + text[end:]

def hook(event, channel, detail, instance='hhcd->Instance'):
    return f'  UsbRxTrace_Record({instance}, {channel}, USB_RX_TRACE_{event}, {detail});'

def save(relative, transform):
    path = root / relative
    text = (Path(__file__).resolve().parent / 'before' / relative).read_text(encoding='utf-8')
    text = transform(text)
    path.write_bytes(text.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8'))

def hcd(text):
    text = text.replace('#include "stm32f4xx_hal.h"',
        '#include "stm32f4xx_hal.h"\n/* 接收故障诊断钩子：仅写 RAM，不改变驱动状态。 */\n#include "usb_rx_trace.h"', 1)
    def channel_init(body):
        anchor = '  __HAL_LOCK(hhcd);\n'
        assert body.count(anchor) == 1
        body = body.replace(anchor, anchor +
            '  UsbRxTrace_ChannelInit(hhcd->Instance, ch_num, epnum, ep_type);\n', 1)
        anchor = '  __HAL_UNLOCK(hhcd);'
        assert body.count(anchor) == 1
        return body.replace(anchor, hook('INIT_AFTER', 'ch_num', '(uint32_t)status') + '\n' + anchor, 1)
    text = change_function(text, 'HAL_StatusTypeDef HAL_HCD_HC_Init(', channel_init)
    def submit(body):
        body = body.replace('{\n', '{\n  HAL_StatusTypeDef trace_status;\n' +
            hook('SUBMIT_BEFORE', 'ch_num', 'length') + '\n', 1)
        old = '  return USB_HC_StartXfer(hhcd->Instance, &hhcd->hc[ch_num], (uint8_t)hhcd->Init.dma_enable);'
        assert old in body
        return body.replace(old, hook('SUBMIT_PREPARED', 'ch_num', 'length') + '\n' +
            '  trace_status = USB_HC_StartXfer(hhcd->Instance, &hhcd->hc[ch_num], (uint8_t)hhcd->Init.dma_enable);\n' +
            hook('SUBMIT_RETURN', 'ch_num', '(uint32_t)trace_status') + '\n  return trace_status;')
    text = change_function(text, 'HAL_StatusTypeDef HAL_HCD_HC_SubmitRequest(', submit)
    def irq(body):
        body = body.replace('  uint32_t tmpreg;\n', '  uint32_t tmpreg;\n' +
            '  uint32_t trace_irq = USBx_HC(chnum)->HCINT & USBx_HC(chnum)->HCINTMSK &\n' +
            '      ~(USB_OTG_HCINT_NAK | USB_OTG_HCINT_ACK);\n' +
            '  if (trace_irq != 0U) {\n  ' + hook('IRQ_ENTER', 'chnum', 'trace_irq') + '\n  }\n', 1)
        old = '          USBx_HC(chnum)->HCCHAR = tmpreg;'
        # 同时覆盖缩进不同的全部 IN 通道重新激活点。
        lines = body.splitlines()
        result, rearm_count = [], 0
        for line in lines:
            if line.strip() == 'USBx_HC(chnum)->HCCHAR = tmpreg;':
                indent = line[:len(line)-len(line.lstrip())]
                result += [indent + hook('REARM_BEFORE', 'chnum', '__LINE__').strip(), line,
                           indent + hook('REARM_AFTER', 'chnum', '__LINE__').strip()]
                rearm_count += 1
            elif line.strip() == 'return;':
                result += ['        if (trace_irq != 0U) {', '        ' + hook('IRQ_EXIT', 'chnum', 'trace_irq'),
                           '        }', line]
            else:
                result.append(line)
        assert rearm_count == 4, rearm_count
        body = '\n'.join(result)
        return body[:-1] + '  if (trace_irq != 0U) {\n  ' + hook('IRQ_EXIT', 'chnum', 'trace_irq') + '\n  }\n}'
    # 跳过文件前面的同名静态声明。
    signature = 'static void HCD_HC_IN_IRQHandler(HCD_HandleTypeDef *hhcd, uint8_t chnum)\n{'
    return change_function(text, signature, irq)

def ll(text):
    text = text.replace('#include "stm32f4xx_hal.h"',
        '#include "stm32f4xx_hal.h"\n/* 接收故障诊断钩子：采样通道启停，不改写硬件。 */\n#include "usb_rx_trace.h"', 1)
    assert '#include "usb_rx_trace.h"' in text
    def start(body):
        old = '  USBx_HC(ch_num)->HCCHAR = tmpreg;'
        assert body.count(old) == 1
        return body.replace(old, hook('START_BEFORE', 'ch_num', 'hc->xfer_len', 'USBx') + '\n' + old + '\n' +
                            hook('START_AFTER', 'ch_num', 'hc->xfer_len', 'USBx'))
    text = change_function(text, 'HAL_StatusTypeDef USB_HC_StartXfer(', start)
    def halt(body):
        old = '  /* In buffer DMA, Channel disable'
        assert old in body
        body = body.replace(old, hook('HALT_ENTER', 'hc_num', 'USB_RX_TRACE_CALLER()', 'USBx') + '\n\n' + old, 1)
        assert body.count('return HAL_OK;') == 2
        lines = []
        for line in body.splitlines():
            if line.strip() == 'return HAL_OK;':
                indent = line[:len(line)-len(line.lstrip())]
                lines.append(indent + hook('HALT_EXIT', 'hc_num', '__LINE__', 'USBx').strip())
            lines.append(line)
        return '\n'.join(lines)
    text = change_function(text, 'HAL_StatusTypeDef USB_HC_Halt(', halt)
    def stop(body):
        anchor = '  (void)USB_DisableGlobalInt(USBx);'
        assert body.count(anchor) == 1
        body = body.replace(anchor, hook('STOP_ENTER', 'USB_RX_TRACE_NO_CHANNEL',
                            'USB_RX_TRACE_CALLER()', 'USBx') + '\n' + anchor, 1)
        anchor = '    USBx_HC(i)->HCCHAR = value;'
        assert body.count(anchor) == 2
        body = body.replace(anchor,
            '  ' + hook('STOP_FLUSH_BEFORE', 'i', 'value', 'USBx') + '\n' + anchor + '\n' +
            '  ' + hook('STOP_FLUSH_AFTER', 'i', 'value', 'USBx'), 1)
        # 第二处写入是原始逐通道 halt 请求；保留所有循环和计数行为。
        split = body.index('  /* Halt all channels')
        body = body[:split] + body[split:].replace(anchor,
            '  ' + hook('STOP_HALT_BEFORE', 'i', 'value', 'USBx') + '\n' + anchor + '\n' +
            '  ' + hook('STOP_HALT_AFTER', 'i', 'value', 'USBx'), 1)
        anchor = '    } while ((USBx_HC(i)->HCCHAR & USB_OTG_HCCHAR_CHENA) == USB_OTG_HCCHAR_CHENA);'
        assert body.count(anchor) == 1
        body = body.replace(anchor, anchor + '\n    UsbRxTrace_StopWait(USBx, i, count);', 1)
        anchor = '  (void)USB_EnableGlobalInt(USBx);'
        assert body.count(anchor) == 1
        return body.replace(anchor, hook('STOP_EXIT', 'USB_RX_TRACE_NO_CHANNEL',
                            '(uint32_t)ret', 'USBx') + '\n' + anchor, 1)
    return change_function(text, 'HAL_StatusTypeDef USB_StopHost(', stop)

def target(text):
    anchor = '/* USER CODE BEGIN Includes */'
    assert anchor in text
    text = text.replace(anchor, anchor + '\n#include "usb_rx_trace.h"', 1)
    for name, event in [('Connect', 'CONNECT'), ('Disconnect', 'DISCONNECT'),
                        ('PortEnabled', 'PORT_ENABLED'), ('PortDisabled', 'PORT_DISABLED')]:
        signature = f'void HAL_HCD_{name}_Callback(HCD_HandleTypeDef *hhcd)\n{{'
        text = change_function(text, signature, lambda body, ev=event:
            body.replace('{\n', '{\n' + hook(ev, 'USB_RX_TRACE_NO_CHANNEL', '0U') + '\n', 1))
    signature = 'void HAL_HCD_HC_NotifyURBChange_Callback('
    return change_function(text, signature, lambda body: body.replace('{\n', '{\n' +
        hook('URB_NOTIFY', 'chnum', '(uint32_t)urb_state') + '\n', 1))

save('Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_hcd.c', hcd)
save('Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_usb.c', ll)
save('USB_HOST/Target/usbh_conf.c', target)
print('Inserted diagnostic hooks into HCD, USB LL and Host callbacks.')
