import sys
from pymodbus.client import ModbusTcpClient
c=ModbusTcpClient(sys.argv[1],port=502,timeout=2); c.connect()
for a in range(990,2110):
  r=c.read_input_registers(a,count=1,device_id=1)
  if not r.isError(): print(a,r.registers[0],end=" | ")
