"""Reproducible DataFlash fixtures with known evidence windows (no private flight data)."""
from pathlib import Path
import math, struct
ROOT=Path(__file__).resolve().parents[1]
FORMATS=[
(129,'MSG','QZ','TimeUS,Message'),
(130,'PARM','QNff','TimeUS,Name,Value,Default'),
(131,'ARM','QB','TimeUS,ArmState'),
(132,'MODE','QBB','TimeUS,Mode,Rsn'),
(133,'ATT','Qffffff','TimeUS,DesRoll,Roll,DesPitch,Pitch,DesYaw,Yaw'),
(134,'POS','QLLfff','TimeUS,Lat,Lng,Alt,RelHomeAlt,RelOriginAlt'),
(135,'GPS','QBBBfLLfff','TimeUS,I,Status,NSats,HDop,Lat,Lng,Alt,Spd,VZ'),
(136,'BAT','QBffffffB','TimeUS,Inst,Volt,Curr,CurrTot,EnrgTot,RemPct,Temp,H'),
(137,'VIBE','QfffIII','TimeUS,VibeX,VibeY,VibeZ,Clip0,Clip1,Clip2'),
(138,'XKF4','QBfffff','TimeUS,C,SV,SP,SH,SM,SVT'),
(139,'POWR','Qff','TimeUS,Vcc,VServo'),
(140,'QPOS','QBfff','TimeUS,State,Dist,TSpd,TAcc'),
(141,'QTUN','QBBfffff','TimeUS,Trn,Ast,ThO,Alt,DAlt,CRt,DCRt'),
(142,'RCOU','QHHHH','TimeUS,C5,C6,C7,C8'),
(143,'ERR','QBB','TimeUS,Subsys,ECode'),
(144,'RATE','Qffffff','TimeUS,RDes,R,PDes,P,YDes,Y'),
(145,'PIDR','Qfffffff','TimeUS,Tar,Act,Err,P,I,D,FF'),
]
CODES={'Q':'Q','B':'B','I':'I','H':'H','f':'f','L':'i','N':'16s','Z':'64s'}
def pack(fmt,values):
 converted=[]
 for code,value in zip(fmt,values):
  if code in 'NZ': value=str(value).encode('ascii')
  if code=='L': value=round(value*1e7)
  converted.append(value)
 return struct.pack('<'+''.join(CODES[c] for c in fmt),*converted)
def generate(path, faulty=False, missing=False):
 formats=[f for f in FORMATS if not missing or f[1] in {'MSG','ARM','ATT'}]
 data=bytearray(); lookup={name:(typeid,fmt) for typeid,name,fmt,columns in formats}
 for typeid,name,fmt,columns in formats:
  size=3+struct.calcsize('<'+''.join(CODES[c] for c in fmt))
  data.extend(b'\xa3\x95\x80'+struct.pack('<BB4s16s64s',typeid,size,name.encode(),fmt.encode(),columns.encode()))
 def add(name,*values):
  if name in lookup:
   typeid,fmt=lookup[name];data.extend(b'\xa3\x95'+bytes([typeid])+pack(fmt,values))
 add('MSG',1000000,'ArduPlane V4.5.7 (synthetic teaching fixture)')
 params={'Q_ENABLE':1,'Q_TAILSIT_ENABLE':0,'Q_FRAME_CLASS':1,'Q_FRAME_TYPE':1,'Q_OPTIONS':0,'Q_TRANS_DECEL':2,'Q_TRANS_FAIL':10,'Q_TRANS_FAIL_ACT':1,'AIRSPEED_MIN':14,'Q_ASSIST_SPEED':15,'ARSPD_TYPE':0,'BATT_MONITOR':4,'BATT_LOW_VOLT':22,'BATT_CRT_VOLT':21,'BATT_LOW_MAH':400,'BATT_CRT_MAH':200,'ARMING_CHECK':1,'Q_M_PWM_MIN':1100,'Q_M_PWM_MAX':1900}
 for c in range(5,9):params.update({f'SERVO{c}_FUNCTION':33+c-5,f'SERVO{c}_MIN':1100,f'SERVO{c}_MAX':1900})
 for name,value in params.items():add('PARM',1000000,name,value,value)
 add('ARM',10000000,1);add('ARM',230000000,0)
 for time,mode in [(1,0),(10,17),(28,10),(170 if faulty else 158,20),(215,19)]:add('MODE',time*1000000,mode,1)
 if faulty:
  add('MSG',110000000,'Failsafe: EKF variance')
  add('MSG',115000000,'Failsafe cleared')
  add('ERR',111000000,12,3);add('ERR',113000000,12,0)
 clip=100
 for k in range(12000):
  t=1+k*0.02;us=round(t*1e6)
  roll=7*math.sin(t*0.15);pitch=2*math.sin(t*0.1)
  actual=roll+0.5*math.sin(t)
  if faulty and 85<=t<=88:actual+=23
  if faulty and t<5:actual+=35 # Ground error must not generate flight tracking findings.
  add('ATT',us,roll,actual,pitch,pitch+0.3*math.sin(t),90,90+0.1*math.sin(t))
  add('RATE',us,roll*0.15,actual*0.15,pitch*0.1,pitch*0.1,0,0)
  add('PIDR',us,roll,actual,roll-actual,(roll-actual)*0.03,0.1,0.01*math.sin(t),roll*0.02)
  if k%5:continue
  if t<10 or t>230:alt=0
  elif t<25:alt=(t-10)*4
  elif t>205:alt=max(0,60-(t-205)*2.4)
  else:alt=60+2*math.sin(t*0.1)
  speed=0 if alt==0 else 5 if t<28 or t>175 else 20+math.sin(t*0.08)
  lat=31.2+0.002*math.sin(t/40);lng=121.5+0.002*math.cos(t/40)
  add('POS',us,lat,lng,alt+15,alt,alt)
  gpsLoss=faulty and 90<=t<=94
  add('GPS',us,0,2 if gpsLoss else 3,6 if gpsLoss else 16,3.5 if gpsLoss else 0.8,lat,lng,alt+15,speed,0)
  add('GPS',us,1,3,19,0.6,lat,lng,alt+15,speed,0)
  voltage=20.4 if faulty and 180<=t<=185 else 25.0-t/200
  add('BAT',us,0,voltage,16+speed*0.6,t/100,t/4,100-t/4,30,1)
  add('BAT',us,1,12.5,2,t/300,t/20,95,28,1)
  # A 100 ms peak between the old 200 ms sampling instants.
  spike=faulty and k==6005
  if spike:clip+=4
  add('VIBE',us,85 if spike else 12+2*math.sin(t),11,15,clip,0,0)
  ekf=1.6 if faulty and 108<=t<=112 else 0.2
  add('XKF4',us,0,ekf,0.2,0.2,0.2,0.2)
  add('XKF4',us,1,0.15,0.2,0.2,0.2,0.2)
  add('POWR',us,4.3 if faulty and 190<=t<=191 else 5.1,5.0)
  trn=0 if t<32 or t>=158 else 2
  if 32<=t<34:trn=1
  if faulty and 155<=t<=169:trn=0
  assisting=1 if 28<=t<34 or (faulty and 155<=t<=169) else 0
  add('QTUN',us,trn,assisting,0.65 if alt>0 else 0.0,alt,60,0,0)
  if t>=158:
   state=3 if t<195 else 4 if t<211 else 5
   distance=max(0,220-(t-158)*4)
   add('QPOS',us,state,distance,max(0,20-(t-158)*0.35),2)
  pwm=1895 if faulty and 65<=t<=70 else 1480 if alt>0 else 1100
  add('RCOU',us,pwm,pwm+2,pwm+3,pwm+1)
 path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
 return len(data)
if __name__=='__main__':
 print('normal:',generate(ROOT/'测试'/'normal.BIN'))
 print('faulty:',generate(ROOT/'示例日志'/'QuadPlane_教学示例.BIN',True))
 print('missing:',generate(ROOT/'测试'/'missing.BIN',missing=True))
 (ROOT/'测试'/'invalid.BIN').write_bytes(b'not a DataFlash log'*20)
 # Structurally valid but no timed records: must be rejected.
 full=(ROOT/'测试'/'normal.BIN').read_bytes();(ROOT/'测试'/'fmt_only.BIN').write_bytes(full[:89*len(FORMATS)])
