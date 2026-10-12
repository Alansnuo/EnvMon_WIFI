# -*- coding: utf-8 -*-
"""生成天问Block积木固件: 定时播报 + 阈值告警 + 语音问答
   用 ElementTree 构造, 保证 XML 结构合法(手拼字符串会数错闭合标签)。"""
import os, sys
import xml.etree.ElementTree as ET

TARGET = r'E:\tianwen\环境播报-语音播报与告警.hd'

_uid = [0]
def nid():
    _uid[0] += 1
    return 'b%03d' % _uid[0]

def blk(btype, kids=None, x=None, y=None, bid=None, **fields):
    b = ET.Element('block', {'type': btype, 'id': bid or nid()})
    if x is not None:
        b.set('x', str(x)); b.set('y', str(y))
    for k, v in fields.items():
        ET.SubElement(b, 'field', {'name': k}).text = v
    for k in (kids or []):
        b.append(k)
    return b

def fld(name, text):
    e = ET.Element('field', {'name': name}); e.text = text; return e

def val(name, child):
    v = ET.Element('value', {'name': name}); v.append(child); return v

def stm(name, child):
    s = ET.Element('statement', {'name': name}); s.append(child); return s

def nxt(child):
    n = ET.Element('next'); n.append(child); return n

def shadow(btype, **fields):
    s = ET.Element('shadow', {'type': btype, 'id': nid()})
    for k, v in fields.items():
        ET.SubElement(s, 'field', {'name': k}).text = v
    return s

def chain(blocks):
    for i in range(len(blocks) - 1):
        blocks[i].append(nxt(blocks[i + 1]))
    return blocks[0]

def mutation(attrs):
    m = ET.Element('mutation')
    for k, v in attrs.items():
        m.set(k, str(v))
    return m

def num(n):
    return blk('math_number', NUM=str(n))

def txt(s):
    return blk('text', TEXT=s)

def note(s):
    b = blk('block_with_textarea')
    b.append(fld('input', s))
    return b

def say(s):
    """播放语音 [文本] —— 生成器会自动建语音资源并 play_audio()"""
    return blk('asr_event_play_audio', audio=s)

def saynum(expr):
    """以[数值模式]播报数字 [expr]"""
    return blk('asr_event_play_num', [val('num', expr)], mode='1')

def wake(sec):
    return blk('enter_wakeup', [val('num', num(sec))])

def sw(expr, cases):
    """cases: [(case_value, body_block), ...]  全部显式列出, 不用 default"""
    kids = [mutation({'elseif': len(cases)}), val('IF0', expr)]
    for i, (cv, body) in enumerate(cases, start=1):
        kids.append(val('IF%d' % i, num(cv)))
        kids.append(stm('DO%d' % i, body))
    return blk('controls_switch_case', kids)

def sendstr(s):
    return blk('serial_println', [val('CONTENT', txt(s))], serial_select='Serial')

def rdint():
    return blk('serial_parseInt_Float', serial_select='Serial', STAT='parseInt')

def delay(ms):
    return blk('haodabit_DELAY1', [val('ioinput', shadow('math_number', NUM=str(ms)))])

def forever(body):
    return blk('controls_repeat_forever', [stm('DO', body)])

def ifdo(cond, body):
    return blk('controls_if', [val('IF0', cond), stm('DO0', body)])

def avail():
    return blk('serial_available', serial_select='Serial')

# ============================================================ setup
setup_body = chain([
    note('//========== 上电初始状态 ==========\n'
         '//唤醒词 1 条 + 指令词 3 条。\n'
         '//⚠️ 三条指令词的"回复语"必须留空 —— 要等 STM32 把数据回传过来之后再播。'),
    blk('asr_speak', VOICE_SET='小蝶-清新女声', vol='10', speed='10'),
    blk('asr_welcom', NAME='欢迎使用环境监测语音助手，请用天问五幺唤醒我。'),
    blk('asr_sleep', NAME='我退下了，请用天问五幺唤醒我'),
    blk('asr_id', ASR='天问五幺', leibie='唤醒词', huifu='我在呢', id='0'),
    blk('asr_id', ASR='当前温度', leibie='命令词', huifu='', id='1'),
    blk('asr_id', ASR='当前湿度', leibie='命令词', huifu='', id='2'),
    blk('asr_id', ASR='当前光照', leibie='命令词', huifu='', id='3'),
])
setup = blk('setup', [stm('DO', setup_body)], x=0, y=0, bid='setup')

# ============================================================ hardware_init
hw_body = chain([
    note('//========== 上电后初始化 ==========\n'
         '//串口0(Serial): TX=PB_5  RX=PB_6, 115200 —— 接 STM32 的 PA9/PA10'),
    blk('vol_set', [val('num', shadow('asr_vol', PIN='7'))]),
    blk('serial_begin', [val('CONTENT', shadow('baud', baud='115200'))],
        serial_select='Serial', TX='PB_5', RX='PB_6'),
])
hw = blk('hardware_init', [stm('DO', hw_body)], x=0, y=430, bid='hwinit')

# ============================================================ ASR_CODE (语音问答)
asr_body = chain([
    note('//语音识别成功时被自动调用一次。\n'
         '//三条指令词各自向 STM32 要一个数据, MCU 回"动作码 2 + 数值",\n'
         '//由 UART_RX 线程负责播报那个数字。'),
    blk('set_state_enter_wakeup', [val('num', num(15))]),
    sw(blk('asr_snid'), [(1, sendstr('TEMP')), (2, sendstr('HUMI')), (3, sendstr('LUX'))]),
])
asrfn = blk('procedures_defnoreturn', [stm('STACK', asr_body)], NAME='ASR_CODE', bid='asrfn')
cm = ET.Element('comment', {'pinned': 'false', 'h': '80', 'w': '160'})
cm.text = '识别到指令词 → 向 STM32 要数据'
asrfn.insert(1, cm)

# ============================================================ 线程 UART_RX
# 内层开关: 环境播报里的光线等级 (0过暗 1适宜 2过强)
light_sw = sw(rdint(), [
    (0, say('光线很暗，请及时开灯')),
    (1, say('光线充足')),
    (2, say('光线过强，请拉上窗帘')),
])
env_body = chain([
    say('当前温度'),
    saynum(rdint()),
    say('摄氏度，湿度百分之'),
    saynum(rdint()),
    light_sw,
])

# 内层开关: 语音告警码 1~7
alarm_sw = sw(rdint(), [
    (1, say('警告，温度过高，请注意通风')),
    (2, say('警告，温度过低，请注意保暖')),
    (3, say('警告，湿度过高，请注意除湿')),
    (4, say('警告，湿度过低，请注意加湿')),
    (5, say('警告，光照不足，已自动补光')),
    (6, say('警告，光照过强，请遮挡直射光')),
    (7, say('环境参数已全部恢复正常')),
])
alarm_body = alarm_sw

# 外层开关: 动作码 0播报环境 / 1语音告警 / 2播报单个数字
main_sw = sw(rdint(), [
    (0, env_body),
    (1, alarm_body),
    (2, saynum(rdint())),
])

rx_body = chain([
    note('//========== 收 STM32 的数据帧并播报 ==========\n'
         '//帧格式: <动作码> <数值...>  空格分隔   例: "0 25 60 1"\n'
         '//   0 = 播报环境(温度/湿度/光线等级)  "0 25 60 1"\n'
         '//   1 = 语音告警(告警码 1~7)         "1 3"\n'
         '//   2 = 只播报一个数字(语音问答)      "2 25"\n'
         '//⚠️ 必须"马上唤醒": 播放语音/播报数字都只在唤醒状态才执行'),
    forever(chain([
        ifdo(avail(), chain([wake(15), main_sw])),
        delay(2),
    ])),
])

rx_thread = blk('thread_app', [stm('DO', rx_body)],
                app_name='UART_RX', uxPriority='4', mem='192', x=0, y=700, bid='rxthr')

# ============================================================ 线程 app
app_body = chain([
    note('//平台要求的空线程'),
    forever(delay(100)),
])
app_thread = blk('thread_app', [stm('DO', app_body)],
                 app_name='app', uxPriority='4', mem='128', x=0, y=1100, bid='appthr')

# ============================================================ 输出
root = ET.Element('xml', {'xmlns': 'http://www.w3.org/1999/xhtml'})
for b in (setup, hw, asrfn, rx_thread, app_thread):
    root.append(b)

body = ET.tostring(root, encoding='unicode')
xml_text = (body + '\n<!-- edittype="asr_pro" -->\n'
                   + '<!-- officialLib="[]" -->\n'
                   + '<!-- personalLib="[]" -->\n')

# ---------------- 自检 ----------------
import re
ET.fromstring(xml_text.split('<!--')[0])          # 结构合法?
types = re.findall(r'<block type="([^"]+)"', xml_text)
print('XML 自检通过, 顶层 %d 块' % len(list(ET.fromstring(xml_text.split("<!--")[0]))))
for t in sorted(set(types)):
    print('   %-28s x%d' % (t, types.count(t)))
print('总长 %d 字符' % len(xml_text))

if not os.path.isdir(os.path.dirname(TARGET)):
    sys.exit('目录不存在')
with open(TARGET, 'w', encoding='utf-8', newline='') as fp:
    fp.write(xml_text)
print('WROTE %s' % TARGET)
