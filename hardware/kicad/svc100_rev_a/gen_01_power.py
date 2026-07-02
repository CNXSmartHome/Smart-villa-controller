#!/usr/bin/env python3
"""Generate a self-contained KiCad 8 schematic for SVC-100 Sheet 01_POWER.

Connectivity is by global labels (each pin gets a short wire stub + a global
label of its net name) and power flags, so it is correct-by-construction without
needing point-to-point routing. All symbols are placed at rotation 0; a pin's
absolute connection point is (inst_x + lx, inst_y - ly) because KiCad symbol
geometry is Y-up while the schematic is Y-down.
"""
import uuid, io

ROOT = str(uuid.uuid4())
def U(): return str(uuid.uuid4())

# ---- symbol definitions: name -> (rect(w,h), pins=[(num,name,lx,ly,etype)]) ----
P_PASS="passive"; P_PIN="power_in"; P_POUT="power_out"
SYMS = {
 "R":   ((2.0,5.08),[("1","~",0,3.81,P_PASS),("2","~",0,-3.81,P_PASS)]),
 "C":   ((2.0,5.08),[("1","~",0,3.81,P_PASS),("2","~",0,-3.81,P_PASS)]),
 "CP":  ((2.0,5.08),[("1","+",0,3.81,P_PASS),("2","-",0,-3.81,P_PASS)]),
 "L":   ((2.0,5.08),[("1","1",0,3.81,P_PASS),("2","2",0,-3.81,P_PASS)]),
 "FUSE":((2.0,5.08),[("1","1",0,3.81,P_PASS),("2","2",0,-3.81,P_PASS)]),
 "TVS": ((2.5,5.08),[("1","A1",0,3.81,P_PASS),("2","A2",0,-3.81,P_PASS)]),
 "DIODE":((2.5,5.08),[("1","K",0,3.81,P_PASS),("2","A",0,-3.81,P_PASS)]),
 "PMOS":((7.62,7.62),[("1","G",-7.62,0,P_PASS),("2","S",0,7.62,P_PASS),("3","D",0,-7.62,P_PASS)]),
 "TP":  ((1.0,1.0),[("1","1",0,2.54,P_PASS)]),
 "FLAG":((1.0,1.0),[("1","1",0,2.54,P_POUT)]),
 "SCREW2":((5.08,7.62),[("1","1",-5.08,2.54,P_PASS),("2","2",-5.08,-2.54,P_PASS)]),
 "SCREW3":((5.08,10.16),[("1","1",-5.08,5.08,P_PASS),("2","2",-5.08,0,P_PASS),("3","3",-5.08,-5.08,P_PASS)]),
 # TPS54360 8-pin buck (left: BOOT,VIN,EN,RT ; right: SW,GND,COMP,FB)
 "BUCK":((15.24,20.32),[
     ("1","BOOT",-10.16,7.62,P_PASS),("2","VIN",-10.16,2.54,P_PIN),
     ("3","EN",-10.16,-2.54,P_PASS),("4","RT/CLK",-10.16,-7.62,P_PASS),
     ("8","SW",10.16,7.62,P_PASS),("7","GND",10.16,2.54,P_PIN),
     ("6","COMP",10.16,-2.54,P_PASS),("5","FB",10.16,-7.62,P_PASS),
     ("9","PAD",10.16,-12.7,P_PIN)]),
 # AP7361C-33 LDO SOT-223 (kept for reference / DNP alt): 1=GND,2=VOUT,3=VIN
 "LDO":((10.16,10.16),[("3","VIN",-7.62,2.54,P_PIN),("1","GND",0,-7.62,P_PIN),
                       ("2","VOUT",7.62,2.54,P_POUT)]),
 # TPS563201 3.3V sync buck (SOT-23-6): 1=GND,2=SW,3=VIN,4=FB,5=EN
 "BUCK33":((12.7,15.24),[("3","VIN",-7.62,5.08,P_PIN),("5","EN",-7.62,-2.54,P_PASS),
                         ("1","GND",0,-7.62,P_PIN),("2","SW",7.62,5.08,P_PASS),
                         ("4","FB",7.62,-2.54,P_PASS)]),
}
DNP = {"TP6","R9"}   # do-not-populate by default

POWER_NETS = {"+5V","+3V3","GND","VIN_PROTECTED","SENSOR_V+","VIN_24V"}

# ---- component placement: (ref, sym, value, footprint, x, y, {pin:net}) ----
C = []
def add(ref,sym,val,fp,x,y,nets): C.append((ref,sym,val,fp,x,y,nets))

# Input + protection (column 1)
add("J1","SCREW2","Conn 24Vin","TerminalBlock:TerminalBlock_bornier-2_P5.08mm",40,60,
    {"1":"VIN_24V","2":"GND"})
add("F1","FUSE","MF-R250 PTC 2.5A","Fuse:Fuse_Bourns_MF-RG300",60,55,
    {"1":"VIN_24V","2":"VIN_F"})
# P-FET reverse protection: Source = protected/load rail, Drain = fused supply,
# Gate pulled to GND via R1, gate-source clamped by D2 (|VGS| < 15 V at 24 Vin).
add("Q1","PMOS","DMP10H700S PMOS 100V","Package_TO_SOT_SMD:SOT-23",85,55,
    {"2":"VIN_PROTECTED","3":"VIN_F","1":"RPP_G"})
add("R1","R","100k gate-GND","Resistor_SMD:R_0603_1608Metric",85,75,
    {"1":"RPP_G","2":"GND"})
add("D2","TVS","SMBJ15CA gate clamp","Diode_SMD:D_SMB",100,72,
    {"1":"VIN_PROTECTED","2":"RPP_G"})
add("D1","TVS","SMBJ30A input","Diode_SMD:D_SMB",110,55,
    {"1":"VIN_PROTECTED","2":"GND"})
add("C1","CP","100uF 50V bulk","Capacitor_SMD:CP_Elec_8x10.5",125,55,
    {"1":"VIN_PROTECTED","2":"GND"})
add("C2","C","10uF 50V","Capacitor_SMD:C_1210_3225Metric",140,55,
    {"1":"VIN_PROTECTED","2":"GND"})
add("C3","C","100nF 50V","Capacitor_SMD:C_0603_1608Metric",152,55,
    {"1":"VIN_PROTECTED","2":"GND"})

# Buck TPS54360 (column 2)
add("U1","BUCK","TPS54360DDA","Package_SO:HSOP-8-1EP_3.9x4.9mm",185,55,
    {"2":"VIN_PROTECTED","1":"BOOT","8":"SW","7":"GND","9":"GND",
     "3":"EN","4":"RT","5":"FB","6":"COMP"})
add("C4","C","100nF boot","Capacitor_SMD:C_0603_1608Metric",168,75,
    {"1":"BOOT","2":"SW"})
add("R2","R","442k EN top","Resistor_SMD:R_0603_1608Metric",160,40,
    {"1":"VIN_PROTECTED","2":"EN"})
add("R3","R","60.4k EN bot","Resistor_SMD:R_0603_1608Metric",160,28,
    {"1":"EN","2":"GND"})
add("R4","R","100k RT (500kHz)","Resistor_SMD:R_0603_1608Metric",172,32,
    {"1":"RT","2":"GND"})
add("D3","DIODE","SS5P6 60V5A catch","Diode_SMD:D_SMC",205,40,
    {"1":"SW","2":"GND"})
add("L1","L","15uH 4A","Inductor_SMD:L_Bourns-SRP1245A",218,55,
    {"1":"SW","2":"+5V"})
add("R5","R","52.3k FB top","Resistor_SMD:R_0603_1608Metric",205,72,
    {"1":"+5V","2":"FB"})
add("R6","R","10k FB bot","Resistor_SMD:R_0603_1608Metric",205,84,
    {"1":"FB","2":"GND"})
add("R7","R","comp 11.3k","Resistor_SMD:R_0603_1608Metric",218,72,
    {"1":"COMP","2":"COMP_C"})
add("C5","C","comp 8.2nF","Capacitor_SMD:C_0603_1608Metric",218,84,
    {"1":"COMP_C","2":"GND"})
add("C6","C","comp 47pF","Capacitor_SMD:C_0603_1608Metric",230,72,
    {"1":"COMP","2":"GND"})
add("C7","C","22uF 16V out","Capacitor_SMD:C_1210_3225Metric",235,55,
    {"1":"+5V","2":"GND"})
add("C8","C","22uF 16V out","Capacitor_SMD:C_1210_3225Metric",247,55,
    {"1":"+5V","2":"GND"})
add("C9","C","100nF","Capacitor_SMD:C_0603_1608Metric",259,55,
    {"1":"+5V","2":"GND"})

# 3.3V synchronous BUCK (replaces LDO for thermal headroom) (column 3)
add("U2","BUCK33","TPS563201 3V3","Package_TO_SOT_SMD:SOT-23-6",290,55,
    {"3":"+5V","5":"EN3V3","1":"GND","2":"SW3","4":"FB3"})
add("C10","C","10uF 16V in","Capacitor_SMD:C_0805_2012Metric",276,40,
    {"1":"+5V","2":"GND"})
add("R10","R","100k EN pu","Resistor_SMD:R_0603_1608Metric",276,72,
    {"1":"+5V","2":"EN3V3"})
add("L2","L","2.2uH 3A","Inductor_SMD:L_Bourns-SRP6540",305,55,
    {"1":"SW3","2":"+3V3"})
add("R11","R","45.3k FB top","Resistor_SMD:R_0603_1608Metric",305,72,
    {"1":"+3V3","2":"FB3"})
add("R12","R","10k FB bot","Resistor_SMD:R_0603_1608Metric",305,84,
    {"1":"FB3","2":"GND"})
add("C11","C","22uF 10V out","Capacitor_SMD:C_0805_2012Metric",318,40,
    {"1":"+3V3","2":"GND"})
add("C12","C","100nF out","Capacitor_SMD:C_0603_1608Metric",330,40,
    {"1":"+3V3","2":"GND"})

# Sensor power branch (column 4) — DEFAULT source = +5V (safe for 5V sensors).
# For a 12-24V sensor: DNP R8, fit R9 (VIN_PROTECTED) and change D4 to SMBJ33A.
add("R8","R","0R +5V src (fit)","Resistor_SMD:R_0603_1608Metric",278,100,
    {"1":"+5V","2":"SENSOR_SEL"})
add("R9","R","0R 12-24V src (DNP)","Resistor_SMD:R_0603_1608Metric",278,112,
    {"1":"VIN_PROTECTED","2":"SENSOR_SEL"})
add("F2","FUSE","MF-R050 PTC 0.5A","Fuse:Fuse_Bourns_MF-RG300",293,100,
    {"1":"SENSOR_SEL","2":"SENSOR_V+"})
add("D4","TVS","SMBJ5.0A (5V default)","Diode_SMD:D_SMB",307,100,
    {"1":"SENSOR_V+","2":"GND"})
add("C13","C","10uF 16V","Capacitor_SMD:C_1210_3225Metric",319,100,
    {"1":"SENSOR_V+","2":"GND"})
add("C14","C","100nF","Capacitor_SMD:C_0603_1608Metric",331,100,
    {"1":"SENSOR_V+","2":"GND"})
add("J2","SCREW2","Sensor pwr (CHECK V)","TerminalBlock:TerminalBlock_bornier-2_P5.08mm",345,100,
    {"1":"SENSOR_V+","2":"GND"})

# Test points
for i,(net,x) in enumerate([("VIN_PROTECTED",130),("+5V",250),("+3V3",300),
                            ("GND",150),("SENSOR_V+",335),("SW",210)]):
    add(f"TP{i+1}","TP",f"TP {net}","TestPoint:TestPoint_Pad_D2.0mm",x,118,{"1":net})

# Power flags (drivers for ERC) on every supply net
for i,(net,x) in enumerate([("VIN_24V",40),("VIN_PROTECTED",120),("+5V",255),
                            ("+3V3",312),("SENSOR_V+",352),("GND",60)]):
    add(f"PWR{i+1}","FLAG","PWR_FLAG","",x,108,{"1":net})

# ---------------------------------------------------------------- emit
def fnum(v): return ("%.4f"%v).rstrip("0").rstrip(".")
out = io.StringIO()
w = out.write
w('(kicad_sch (version 20231120) (generator "svc100_power_gen") (generator_version "8.0")\n')
w('  (uuid "%s")\n  (paper "A3")\n' % U())
w('  (title_block (title "SVC-100 Rev A - 01 POWER") (rev "A") (company "Smart Villa OS")\n')
w('    (comment 1 "24VDC in -> PTC -> PMOS RPP -> TVS -> TPS54360 5V -> TPS563201 3V3 -> sensor pwr"))\n')

# lib_symbols
w('  (lib_symbols\n')
for name,(rect,pins) in SYMS.items():
    hw,hh = rect[0]/2.0, rect[1]/2.0
    w('    (symbol "svc:%s" (pin_numbers hide) (pin_names (offset 1.016)) '
      '(exclude_from_sim no) (in_bom yes) (on_board yes)\n' % name)
    w('      (property "Reference" "%s" (at 0 %s 0) (effects (font (size 1.27 1.27))))\n'
      % ("U" if name in("BUCK","LDO") else "X", fnum(hh+2.54)))
    w('      (property "Value" "%s" (at 0 %s 0) (effects (font (size 1.27 1.27))))\n'
      % (name, fnum(-hh-2.54)))
    w('      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n')
    w('      (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n')
    w('      (symbol "%s_0_1"\n' % name)
    if name not in ("TP","FLAG"):
        w('        (rectangle (start %s %s) (end %s %s) (stroke (width 0.254) (type default)) (fill (type none)))\n'
          % (fnum(-hw),fnum(hh),fnum(hw),fnum(-hh)))
    w('      )\n      (symbol "%s_1_1"\n' % name)
    for num,pn,lx,ly,et in pins:
        # angle so pin points away from body
        if abs(lx) >= abs(ly): ang = 0 if lx>0 else 180
        else: ang = 90 if ly>0 else 270
        w('        (pin %s line (at %s %s %d) (length 2.54) '
          '(name "%s" (effects (font (size 1.016 1.016)))) '
          '(number "%s" (effects (font (size 1.016 1.016)))))\n'
          % (et, fnum(lx), fnum(ly), ang, pn, num))
    w('      )\n    )\n')
w('  )\n')

wires=[]; labels=[]
def add_wire(x1,y1,x2,y2):
    wires.append((x1,y1,x2,y2))
def add_label(net,x,y,justify):
    labels.append((net,x,y,justify))

for ref,sym,val,fp,x,y,nets in C:
    rect,pins = SYMS[sym]
    w('  (symbol (lib_id "svc:%s") (at %s %s 0) (unit 1) (exclude_from_sim no) '
      '(in_bom yes) (on_board yes) (dnp %s) (uuid "%s")\n'
      % (sym,fnum(x),fnum(y),("yes" if ref in DNP else "no"),U()))
    w('    (property "Reference" "%s" (at %s %s 0) (effects (font (size 1.27 1.27))))\n'
      % (ref,fnum(x),fnum(y-rect[1]/2.0-3.0)))
    w('    (property "Value" "%s" (at %s %s 0) (effects (font (size 1.016 1.016))))\n'
      % (val.replace('"',"'"),fnum(x),fnum(y+rect[1]/2.0+3.0)))
    w('    (property "Footprint" "%s" (at %s %s 0) (effects (font (size 1.016 1.016)) hide))\n'
      % (fp,fnum(x),fnum(y)))
    for num,pn,lx,ly,et in pins:
        w('    (pin "%s" (uuid "%s"))\n' % (num,U()))
    w('    (instances (project "svc100_rev_a" (path "/%s" (reference "%s") (unit 1))))\n' % (ROOT,ref))
    w('  )\n')
    # stubs + labels
    for num,pn,lx,ly,et in pins:
        net = nets.get(num)
        if net is None: continue
        px,py = x+lx, y-ly
        if abs(lx)>=abs(ly):
            dx = 5.08 if lx>=0 else -5.08; ex,ey=px+dx,py; just="left" if dx>0 else "right"
        else:
            dy = 5.08 if ly>0 else -5.08; ex,ey=px,py-dy; just="left"
        add_wire(px,py,ex,ey)
        add_label(net,ex,ey,just)

for x1,y1,x2,y2 in wires:
    w('  (wire (pts (xy %s %s) (xy %s %s)) (stroke (width 0) (type default)) (uuid "%s"))\n'
      % (fnum(x1),fnum(y1),fnum(x2),fnum(y2),U()))
for net,x,y,just in labels:
    w('  (global_label "%s" (shape bidirectional) (at %s %s 0) '
      '(effects (font (size 1.27 1.27)) (justify %s)) (uuid "%s")\n'
      % (net,fnum(x),fnum(y),just,U()))
    w('    (property "Intersheetrefs" "${INTERSHEET_REFS}" (at 0 0 0) '
      '(effects (font (size 1.27 1.27)) hide)))\n')

w('  (sheet_instances (path "/" (page "1")))\n')
w(')\n')

data = out.getvalue()
path = "/sessions/beautiful-adoring-allen/mnt/Projects/svc-100/hardware/kicad/svc100_rev_a/sheets/01_POWER.kicad_sch"
with open(path,"w") as f: f.write(data)

# self-check: balanced parens + token sanity
bal=0; ok=True
for ch in data:
    if ch=='(':bal+=1
    elif ch==')':
        bal-=1
        if bal<0: ok=False;break
print("components:",len(C),"wires:",len(wires),"labels:",len(labels))
print("paren_balance:",bal,"ok:",ok and bal==0)
print("bytes:",len(data),"->",path)
