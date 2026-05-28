The original intent of this project was to enable bytewise editing of the memory 
and RTC registers of Dallas timekeeping RAM parts used in a lot of older test equipment
such as the DS1644 and DS1742 used in the Tektronix TDS300 and TDS3000 series oscilloscopes.
In the process of implementing that I realized it was trivial to allow reading virtually any
parallel memory device of 32 pins or less and writing to any SRAM or NVRAM so I added support
for other devices I had on hand. 
While it is usually possible to dump the contents of Dallas devices with a TL866 or similar
device programmer, attempting to write to one will stomp on the RTC and status bytes which
can result in the chip's test mode being enabled or other issues. With this you can view and
manipulate individual bytes such as the power on hours of the TDS3000, view and set the 
time/date/day, and activate the test mode while measuring the frequency. I have tried to make
it relatively simple to add support for additional parts and I will try to do so as examples
come into my possession.
   

