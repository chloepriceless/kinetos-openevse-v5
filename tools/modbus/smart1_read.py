import sys
from pymodbus.client import ModbusTcpClient
c=ModbusTcpClient(sys.argv[1],port=502,timeout=3); print("connect",c.connect())
for a,n in [(999,8),(1099,13),(1220,9),(1599,4),(1999,4)]:
    r=c.read_holding_registers(a,count=n,device_id=1); print(a, r if r.isError() else r.registers)
r=c.read_input_registers(1000,count=1,device_id=1); print("FC4 1000", r.registers if not r.isError() else r)
r=c.read_holding_registers(5000,count=1,device_id=1); print("illegal", r)
