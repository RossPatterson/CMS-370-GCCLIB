#!/bin/sh
# Make GCCLIB on CMS

# Exit if there is an error
set -e

# Show the commands
set -x

### TEMP
# Install HercControl
wget -nv https://raw.githubusercontent.com/RossPatterson/PyHercControl/refs/tags/v1.1.2/PyHercControl/src/herccontrol
chmod +x herccontrol
mv herccontrol /usr/local/bin
### TEMP


# IPL
herccontrol "ipl 6a1" -w "USER DSC LOGOFF AS AUTOLOG1"
herccontrol "/cp start c" -w "RDR"
herccontrol "/cp start d class a" -w "PUN"

# LOGON MAINTC
herccontrol "/cp disc" -w "^VM/370 Online"
herccontrol "/logon maintc maintc" -w "^VM Community Edition"
herccontrol "/" -w "^Ready;"
herccontrol "/purge rdr" -w "^Ready;"
herccontrol "/ACCESS 394 A" -w "^Ready;"
herccontrol "/ERASE * * A1" -w "^Ready;"

# Send Source Code - including tools
cp tools/* .
yata -c
herccontrol -m >tmp; read mark <tmp; rm tmp
echo "USERID  MAINTC\n:READ  ARCHIVE  YATA    " > tmp
cat archive.yata >> tmp
netcat -q 0 localhost 3505 < tmp
rm tmp
herccontrol -w "HHCRD012I" -f $mark
herccontrol "/" -w "RDR FILE"

# Prepare Source
herccontrol "/yata -x -f READER -d a" -w "^Ready;" -t 120
herccontrol "/COPYFILE * MACRO    A (RECFM F LRECL 80" -w "^Ready"
herccontrol "/COPYFILE * COPY     A (RECFM F LRECL 80" -w "^Ready"
herccontrol "/COPYFILE * ASSEMBLE A (RECFM F LRECL 80" -w "^Ready"
#herccontrol "/COPYFILE * EXEC     A (RECFM F" -w "^Ready"

# Make source tape and vmarc
herccontrol "/cp disc" -w "^VM/370 Online"
herccontrol "/logon operator operator" -w "RECONNECTED AT"
hetinit -n -d gcclibsrc.aws
herccontrol "devinit 480 io/gcclibsrc.aws" -w "^HHCPN098I"
herccontrol "/attach 480 to maintc as 181" -w "TAPE 480 ATTACH"
herccontrol "devinit 00d io/gcclibsrc.vmarc" -w "^HHCPN098I"
herccontrol "/cp disc" -w "^VM/370 Online"
herccontrol "/logon maintc maintc" -w "RECONNECTED AT"
herccontrol "/begin"
herccontrol "/tape dump * * a (noprint" -w "^Ready;"
herccontrol "/detach 181" -w "^Ready;"
herccontrol "/vmarc pack * * a (pun notrace" -w "^Ready;"

# Close and remove extra record from VMARC file
herccontrol "devinit 00d dummy" -w "^HHCPN098I"
truncate -s-80 gcclibsrc.vmarc

# Put tools in the T drive
herccontrol "/COPYFILE GCCASM EXEC A = = T (REPLACE" -w "^Ready"
herccontrol "/ERASE GCCASM EXEC A" -w "^Ready"
herccontrol "/COPYFILE GCCASM HELPCMD A = = T (REPLACE" -w "^Ready"
herccontrol "/ERASE GCCASM HELPCMD A" -w "^Ready"

herccontrol "/COPYFILE GCCBUILD EXEC A = = T (REPLACE" -w "^Ready"
herccontrol "/ERASE GCCBUILD EXEC A" -w "^Ready"
herccontrol "/COPYFILE GCCBUILD HELPCMD A = = T (REPLACE" -w "^Ready"
herccontrol "/ERASE GCCBUILD HELPCMD A" -w "^Ready"

herccontrol "/COPYFILE GCCCOMP EXEC A = = T (REPLACE" -w "^Ready"
herccontrol "/ERASE GCCCOMP EXEC A" -w "^Ready"
herccontrol "/COPYFILE GCCCOMP HELPCMD A = = T (REPLACE" -w "^Ready"
herccontrol "/ERASE GCCCOMP HELPCMD A" -w "^Ready"

herccontrol "/COPYFILE GCCGEN EXEC A = = T (REPLACE" -w "^Ready"
herccontrol "/ERASE GCCGEN EXEC A" -w "^Ready"
herccontrol "/COPYFILE GCCGEN HELPCMD A = = T (REPLACE" -w "^Ready"
herccontrol "/ERASE GCCGEN HELPCMD A" -w "^Ready"

herccontrol "/COPYFILE GCCGENM EXEC A = = T (REPLACE" -w "^Ready"
herccontrol "/ERASE GCCGENM EXEC A" -w "^Ready"
herccontrol "/COPYFILE GCCGENM HELPCMD A = = T (REPLACE" -w "^Ready"
herccontrol "/ERASE GCCGENM HELPCMD A" -w "^Ready"

herccontrol "/COPYFILE GCCSRCH EXEC A = = T (REPLACE" -w "^Ready"
herccontrol "/ERASE GCCSRCH EXEC A" -w "^Ready"
herccontrol "/COPYFILE GCCSRCH HELPCMD A = = T (REPLACE" -w "^Ready"
herccontrol "/ERASE GCCSRCH HELPCMD A" -w "^Ready"

herccontrol "/ipl cms" -w "^VM Community Edition"
herccontrol "/" -w "^Ready;"

herccontrol "/GCCBUILD" -w "^Ready;" -t 240
herccontrol "/GCCGENM" -w "^Ready;"

herccontrol "/ipl cms" -w "^VM Community Edition"
herccontrol "/" -w "^Ready;"

herccontrol "/GCCSRCH" -w "^Ready;"
herccontrol "/GCCGEN" -w "^Ready;"

herccontrol "/ipl cms" -w "^VM Community Edition"
herccontrol "/" -w "^Ready;"
# Drop bREXX in case it is incompatible with the new GCCLIB.
herccontrol "/RESLIB DEL DMSREX" -w "^Ready;"

# Make binary tape and vmarc
herccontrol "/cp disc" -w "^VM/370 Online"
herccontrol "/logon operator operator" -w "RECONNECTED AT"
hetinit -n -d gcclibbin.aws
herccontrol "devinit 480 io/gcclibbin.aws" -w "^HHCPN098I"
herccontrol "/attach 480 to maintc as 181" -w "TAPE 480 ATTACH"
herccontrol "devinit 00d io/gcclibbin.vmarc" -w "^HHCPN098I"
herccontrol "/cp disc" -w "^VM/370 Online"
herccontrol "/logon maintc maintc" -w "RECONNECTED AT"
herccontrol "/begin"
herccontrol "/access 194 e" -w "^Ready;"
herccontrol "/copyfile gcclib * a = = e" -w "^Ready;"
herccontrol "/copyfile gccres * a = = e" -w "^Ready;"
herccontrol "/tape dump * * e (noprint" -w "^Ready;"
herccontrol "/detach 181" -w "^Ready;"
herccontrol "/vmarc pack * * e (pun notrace" -w "^Ready;"

# Close and remove extra record from VMARC file
herccontrol "devinit 00d dummy" -w "^HHCPN098I"
truncate -s-80 gcclibbin.vmarc

# TEMPORARY!  Build GCCCSECT MODULE, until a new VM/CE release ships our version.
herccontrol "/GCCSRCH" -w "^Ready;"
herccontrol "/MKGCCCS" -w "^Ready;"

# Deploy the new GCCLIB
herccontrol "/cp disc" -w "^VM/370 Online"
herccontrol "/logon operator operator" -w "RECONNECTED AT"
herccontrol "/purge maint rdr" -w "FILES PURGED"
herccontrol "/cp disc" -w "^VM/370 Online"
herccontrol "/logon maintc maintc" -w "RECONNECTED AT"
herccontrol "/begin"
herccontrol "/cp spool punch to maint cont" -w "^Ready;"
herccontrol "/disk dump gcclib txtlib e" -w "^Ready;"
herccontrol "/disk dump gcclib text e" -w "^Ready;"
herccontrol "/disk dump gccres txtlib e" -w "^Ready;"
herccontrol "/cp spool punch close" -w "^Ready;"
herccontrol "/disc" -w "^VM/370 Online"
herccontrol "/logon maint cpcms" -w "^VM Community Edition"
herccontrol "/" -w "^Ready"
herccontrol "/disk load" -w "^Ready"
herccontrol "/access 19e y" -w "^Ready"
herccontrol "/copyfile gcclib txtlib a = = y2 (olddate replace" -w "^Ready;"
herccontrol "/copyfile gcclib text a = = y2 (olddate replace" -w "^Ready;"
herccontrol "/copyfile gccres txtlib a = = y2 (olddate replace" -w "^Ready;"
herccontrol "/access 19e y/s" -w "^Ready"
herccontrol "/define storage 16m"  -w "CP ENTERED"
herccontrol "/ipl 190 clear" -w "^VM Community Edition"
herccontrol "/savesys cms" -w "^VM Community Edition"
herccontrol "/access (noprof" -w "^Ready;"
herccontrol "/disc" -w "^VM/370 Online"

# Build the tests
herccontrol "/logon maintc maintc" -w "RECONNECTED AT"
herccontrol "/begin"
herccontrol "/profile" -w "^Ready;"
herccontrol "/GCCSRCH" -w "^Ready;"
herccontrol "/MKTEST" -w "^Ready;" -t 240 --debug
herccontrol "/logoff" -w "^VM/370 Online"

# Run tests with GCCLIB inside the VM
herccontrol "/logon maintc maintc noipl"  -w "LOGON AT"
herccontrol "/define storage 16m"  -w "CP ENTERED"
herccontrol "/ipl cms" -w "^Ready;"
herccontrol "/access (noprof" -w "^Ready;"
herccontrol "/SET LDRTBLS 64" -w "^Ready;"
herccontrol "/NUCXTEXT GCCLIB ( SYSTEM PERM" -w "^Ready;"
herccontrol "/profile" -w "^Ready;"
herccontrol "/purge rdr" -w "^Ready;"
# Note: This next one accepts RC > 0.  We'll remove that when the tests are cleaned up.
herccontrol "/RUNTEST" -w "^Ready"
herccontrol "/logoff" -w "^VM/370 Online"

# Deploy the new GCCLIB DCSS
herccontrol "/logon maint cpcms" -w "RECONNECTED AT"
herccontrol "/define storage 16m"  -w "CP ENTERED"
herccontrol "/ipl cms" -w "^Ready;"
herccontrol "/access (noprof" -w "^Ready;"
herccontrol "/gccseg f20000" -w "^Ready;"
herccontrol "/logoff" -w "^VM/370 Online"

# Run tests with GCCLIB in the DCSS
herccontrol "/logon maintc maintc"  -w "^VM Community Edition"
herccontrol "/access (noprof" -w "^Ready;"
herccontrol "/SET LDRTBLS 64" -w "^Ready;"
herccontrol "/SEGMENT LOAD GCCLIB ( SYSTEM SHARE" -w "^Ready;"
herccontrol "/profile" -w "^Ready;"
herccontrol "/GCCSRCH" -w "^Ready;"
herccontrol "/purge rdr" -w "^Ready;"
# Note: This next one accepts RC > 0.  We'll remove that when the tests are cleaned up.
herccontrol "/RUNTEST" -w "^Ready"
herccontrol "/logoff" -w "^VM/370 Online"

# SHUTDOWN
herccontrol "/logon operator operator" -w "RECONNECTED AT"
herccontrol "/shutdown" -w "^HHCCP011I"
herccontrol "detach 09F0"
