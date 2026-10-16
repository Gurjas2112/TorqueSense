import asyncio, time
from asyncua import Client, ua
URL = "opc.tcp://127.0.0.1:14840"
async def main():
    async with Client(url=URL) as c:
        eps = await c.get_endpoints()
        print("advertised endpoint URL:", sorted({e.EndpointUrl for e in eps}))
        node = lambda n: c.get_node(f"ns=2;s=Station.W14.{n}")
        print("before first beat  -> Amber:", await node("Andon.Amber").read_value(),
              "| Online:", await node("InspectionOnline").read_value())
        hb = node("Heartbeat")
        for i in range(1, 8):                                   # engine beating every 400 ms
            await hb.write_value(ua.DataValue(ua.Variant(i, ua.VariantType.UInt32), StatusCode=None, SourceTimestamp=None, ServerTimestamp=None))
            await asyncio.sleep(0.4)
        print("while beating      -> Amber:", await node("Andon.Amber").read_value(),
              "| Online:", await node("InspectionOnline").read_value())
        t0 = time.monotonic()                                   # engine dies here
        while not await node("Andon.Amber").read_value():
            await asyncio.sleep(0.05)
        print(f"engine stopped     -> Amber after {time.monotonic() - t0:.2f} s "
              f"| Online: {await node('InspectionOnline').read_value()}")
asyncio.run(main())
