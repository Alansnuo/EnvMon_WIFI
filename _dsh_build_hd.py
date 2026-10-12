# -*- coding: utf-8 -*-
"""用 ElementTree 生成天问Block积木工程(.hd), 保证 XML 结构一定合法。
   手工拼字符串容易数错闭合标签 —— 这就是上一版失败的原因。"""
import os, shutil, sys
import xml.etree.ElementTree as ET

TARGET = r'E:\tianwen\环境播报-当前温度湿度光照.hd'
BACKUP = TARGET + '.bak'

# ---------------- 小工具 ----------------
def blk(btype, bid, kids=None, x=None, y=None, **fields):
    b = ET.Element('block', {'type': btype, 'id': bid})
    if x is not None:
        b.set('x', str(x)); b.set('y', str(y))
    for k, v in fields.items():
        ET.SubElement(b, 'field', {'name': k}).text = v
    for k in (kids or []):
        b.append(k)
    return b

def fld(name, text):
    return ET.Element('field', {'name': name}) if text == '' else _fld(name, text)

def _fld(name, text):
    e = ET.Element('field', {'name': name}); e.text = text; return e

def val(name, child):
    v = ET.Element('value', {'name': name}); v.append(child); return v

def stm(name, child):
    s = ET.Element('statement', {'name': name}); s.append(child); return s

def nxt(child):
    n = ET.Element('next'); n.append(child); return n

def shadow(btype, bid, **fields):
    """影子输入必须用 <shadow> 标签, 用 <block> 会显示成实心积木(能跑但不好看)"""
    s = ET.Element('shadow', {'type': btype, 'id': bid})
    for k, v in fields.items():
        ET.SubElement(s, 'field', {'name': k}).text = v
    return s

def chain(blocks):
    """把若干积木用 <next> 串起来, 返回第一个"""
    for i in range(len(blocks) - 1):
        blocks[i].append(nxt(blocks[i + 1]))
    return blocks[0]

def mutation(attrs):
    m = ET.Element('mutation')
    for k, v in attrs.items():
        m.set(k, str(v))
    return m

# ---------------- 上电初始状态 setup ----------------
setup_txt = blk('block_with_textarea', 'a02')
_fld_ = ET.SubElement(setup_txt, 'field', {'name': 'input'})
_fld_.text = ('//========== 上电初始状态 ==========\n'
              '//唤醒词 1 条 + 指令词 3 条。\n'
              '//⚠️ 三条指令词的"回复语"必须留空 —— 要等 STM32 把数值回传过来之后再播,\n'
              '//   不能识别成功就自动播一句固定的话。')

setup_chain = chain([
    setup_txt,
    blk('asr_speak', 'a03', VOICE_SET='小蝶-清新女声', vol='10', speed='10'),
    blk('asr_welcom', 'a04', NAME='欢迎使用环境监测语音助手，请用天问五幺唤醒我。'),
    blk('asr_sleep', 'a05', NAME='我退下了，请用天问五幺唤醒我'),
    blk('asr_id', 'a06', ASR='天问五幺', leibie='唤醒词', huifu='我在呢', id='0'),
    blk('asr_id', 'a07', ASR='当前温度', leibie='命令词', huifu='', id='1'),
    blk('asr_id', 'a08', ASR='当前湿度', leibie='命令词', huifu='', id='2'),
    blk('asr_id', 'a09', ASR='当前光照', leibie='命令词', huifu='', id='3'),
])
setup = blk('setup', 'a01', kids=[stm('DO', setup_chain)], x=0, y=0)

# ---------------- 上电后初始化 hardware_init ----------------
hw_txt = blk('block_with_textarea', 'a21')
_f = ET.SubElement(hw_txt, 'field', {'name': 'input'})
_f.text = ('//========== 上电后初始化 ==========\n'
           '//串口0(Serial): TX=PB_5  RX=PB_6, 115200 —— 接 STM32 的 PA9/PA10\n'
           '//⚠️ STM32 的 printf 也在同一根线上! 播报时它会把调试信息一起收进来。\n'
           '//   验证语音之前, 请把 STM32 那侧 ESP8266_DEBUG 改回 0, 并尽量减少周期打印。')

volset = blk('vol_set', 'a22', kids=[val('num', shadow('asr_vol', 'a23', PIN='7'))])
serbeg = blk('serial_begin', 'a24', serial_select='Serial', TX='PB_5', RX='PB_6',
             kids=[val('CONTENT', shadow('baud', 'a25', baud='115200'))])
hw = blk('hardware_init', 'a20', kids=[stm('DO', chain([hw_txt, volset, serbeg]))], x=0, y=430)

# ---------------- ASR_CODE ----------------
asr_txt = blk('block_with_textarea', 'a31')
_f = ET.SubElement(asr_txt, 'field', {'name': 'input'})
_f.text = ('//语音识别成功时被自动调用一次。\n'
           '//三条指令词各自向 STM32 要一个数据: 发 "TEMP" / "HUMI" / "LUX"(自动换行)。')

wakeup = blk('set_state_enter_wakeup', 'a32',
             kids=[val('num', shadow('math_number', 'a33', NUM='15'))])

# case 1/2/3 → 串口打印字符串
cases = []
for i, (num, txt) in enumerate([('1', 'TEMP'), ('2', 'HUMI'), ('3', 'LUX')]):
    cases.append(val('IF%d' % (i + 1), blk('math_number', 'a3%d' % (6 + i * 3), NUM=num)))
    send = blk('serial_println', 'a3%d' % (7 + i * 3), serial_select='Serial',
               kids=[val('CONTENT', blk('text', 'a3%d' % (8 + i * 3), TEXT=txt))])
    cases.append(stm('DO%d' % (i + 1), send))

switch = blk('controls_switch_case', 'a34', kids=[mutation({'elseif': 3}),
             val('IF0', blk('asr_snid', 'a35'))] + cases)

asrfn = blk('procedures_defnoreturn', 'a30', NAME='ASR_CODE', x=0, y=700,
            kids=[mutation(None)] if False else [])
# comment 要放在 NAME 之后、STACK 之前
cm = ET.Element('comment', {'pinned': 'false', 'h': '80', 'w': '160'})
cm.text = '识别到指令词 → 向 STM32 要数据'
asrfn.append(cm)
asrfn.append(stm('STACK', chain([asr_txt, wakeup, switch])))

# ---------------- 线程 UART_RX: 收数值并播报 ----------------
rx_txt = blk('block_with_textarea', 'a51')
_f = ET.SubElement(rx_txt, 'field', {'name': 'input'})
_f.text = ('//========== 收 STM32 回传的数值并播报 ==========\n'
           '//STM32 回 "25" → 这里以【数值模式】播报数字 25 → 喇叭念"二十五"')

avail = blk('serial_available', 'a54', serial_select='Serial')
playnum = blk('asr_event_play_num', 'a55', mode='1',
              kids=[val('num', blk('serial_parseInt_Float', 'a56',
                                   serial_select='Serial', STAT='parseInt'))])
ifblk = blk('controls_if', 'a53', kids=[val('IF0', avail), stm('DO0', playnum)])
delay2 = blk('haodabit_DELAY1', 'a57',
             kids=[val('ioinput', shadow('math_number', 'a58', NUM='2'))])
forever = blk('controls_repeat_forever', 'a52', kids=[stm('DO', chain([ifblk, delay2]))])
rx_thread = blk('thread_app', 'a50', app_name='UART_RX', uxPriority='4', mem='128',
                x=0, y=1050, kids=[stm('DO', chain([rx_txt, forever]))])

# ---------------- 线程 app: 平台要求的空线程 ----------------
app_txt = blk('block_with_textarea', 'a61')
_f = ET.SubElement(app_txt, 'field', {'name': 'input'})
_f.text = '//平台要求的空线程'
delay100 = blk('haodabit_DELAY1', 'a63',
               kids=[val('ioinput', shadow('math_number', 'a64', NUM='100'))])
forever2 = blk('controls_repeat_forever', 'a62', kids=[stm('DO', delay100)])
app_thread = blk('thread_app', 'a60', app_name='app', uxPriority='4', mem='128',
                 x=0, y=1400, kids=[stm('DO', chain([app_txt, forever2]))])

# ---------------- 组装 ----------------
root = ET.Element('xml', {'xmlns': 'http://www.w3.org/1999/xhtml'})
for b in (setup, hw, asrfn, rx_thread, app_thread):
    root.append(b)

body = ET.tostring(root, encoding='unicode')
xml_text = (body + '\n'
            + '<!-- edittype="asr_pro" -->\n'
            + '<!-- officialLib="[]" -->\n'
            + '<!-- personalLib="[]" -->\n')

# ------------- 自检: 重新解析 + 统计 -------------
import re
ET.fromstring(xml_text.split('<!--')[0])
types = re.findall(r'<block type="([^"]+)"', xml_text)
print('XML 自检通过')
print('顶层积木块: %d' % len(list(ET.fromstring(xml_text.split("<!--")[0]))))
print('用到的积木类型:')
for t in sorted(set(types)):
    print('   %-28s x%d' % (t, types.count(t)))
print('总长: %d 字符' % len(xml_text))

# ------------- 备份 + 写入 -------------
d = os.path.dirname(TARGET)
if not os.path.isdir(d):
    sys.exit('目录不存在: %s' % d)
if os.path.exists(TARGET):
    if not os.path.exists(BACKUP):
        shutil.copy2(TARGET, BACKUP); print('BACKUP -> %s' % BACKUP)
    else:
        print('BACKUP 已存在, 保留: %s' % BACKUP)
else:
    print('新文件, 无需备份')

with open(TARGET, 'w', encoding='utf-8', newline='') as fp:
    fp.write(xml_text)
print('WROTE %s' % TARGET)
