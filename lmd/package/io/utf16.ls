// UTF-16 code units decode to Unicode scalars, including surrogate pairs (S2.5.8).
pub fn decode(bytes, little_endian = false) string^ {
    let units = [for (i in range(0,int(floor(len(bytes)/2)),1))
        int(bytes[2*i+(if (little_endian) 1 else 0)])*256 + int(bytes[2*i+(if (little_endian) 0 else 1)])]
    let invalid = len(bytes) % 2 != 0 or any([for (i,unit in units)
        if (unit >= 55296 and unit < 56320)
            i+1 >= len(units) or units[i+1] < 56320 or units[i+1] >= 57344
        else if (unit >= 56320 and unit < 57344)
            i == 0 or units[i-1] < 55296 or units[i-1] >= 56320
        else false]);
    if (invalid) raise error("utf16: invalid code unit sequence")
    else join([for (i,unit in units)
        if (unit >= 56320 and unit < 57344) ""
        else if (unit >= 55296 and unit < 56320)
            chr(65536+(unit-55296)*1024+(units[i+1]-56320))
        else chr(unit)],"")
}
