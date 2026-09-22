import sys
from pymodbus.client import ModbusTcpClient
c=ModbusTcpClient(sys.argv[1],port=502,timeout=2); c.connect()
cands=list(range(0,300))+list(range(300,65500,50))
for fn in ("read_holding_registers","read_input_registers"):
  hits=[]
  for a in cands:
    r=getattr(c,fn)(a,count=1,device_id=1)
    if not r.isError(): hits.append((a,r.registers[0]))
  print(fn, len(hits), hits[:60])
for dev in (0,2,255):
  r=c.read_holding_registers(0,count=1,device_id=dev); print("dev",dev,r)
