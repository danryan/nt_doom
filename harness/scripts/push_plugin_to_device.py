'''
Vendored verbatim from github.com/expertsleepersltd/distingNT@abe311cf
  tools/push_plugin_to_device.py
Upstream license preserved below. Re-pull if vendor updates.

MIT License

Copyright (c) 2025 Roger Arnett
Copyright (c) 2025 Expert Sleepers Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
'''

'''
Requirements:
Disting NT Firmware v1.13 or later (SYSEX command to rescan plug-ins added in v1.13)

pip install mido
pip install python-rtmidi

Example usages:

python push_plugin_to_device 0 "/local/plugin/path/plugin.o"
  This will upload the plug-in and reload the current preset.  Any unsaved changes are lost.

python push_plugin_to_device 0 "/local/plugin/path/plugin.o" "devpreset"
  This will save your current state to a preset named "devpreset", upload the plug-in
  and then reload the preset "devpreset" after uploading.
'''


import mido
import os
import sys
from pathlib import PurePath
import time

sysExId = int(sys.argv[1])
local_plugin_path = sys.argv[2]

tempPresetName = None
if len(sys.argv) > 3:
    tempPresetName = sys.argv[3]


nt_plugin_path = "/programs/plug-ins/" + PurePath(local_plugin_path).name

# Local modification (not upstream): select a free disting NT USB-MIDI port
# instead of always the first match, so the upload can run alongside another
# MIDI client (e.g. nt_helper) when the NT exposes more than one port. The
# substring stays "disting NT"; port names may carry a numeric suffix, i.e.
# "disting NT 5". Set NT_SYSEX_PORT to force a specific port name.
def _nt_ports( names ):
    forced = os.environ.get( "NT_SYSEX_PORT" )
    if forced:
        return [ n for n in names if forced in n ]
    return [ n for n in names if "disting NT" in n ]

out_candidates = _nt_ports( mido.get_output_names() )
in_names = _nt_ports( mido.get_input_names() )
if not out_candidates or not in_names:
    sys.exit( "No 'disting NT' MIDI port found (set NT_SYSEX_PORT to override)." )

outPort = inPort = outPortName = inPortName = None
for name in out_candidates:
    in_match = name if name in in_names else in_names[ 0 ]
    try:
        o = mido.open_output( name )
        i = mido.open_input( in_match )
    except ( IOError, OSError ):
        # Port busy under an exclusive-open backend; try the next one.
        continue
    outPort, inPort, outPortName, inPortName = o, i, name, in_match
    break

if outPort is None:
    sys.exit( "All 'disting NT' MIDI ports are busy; free one (disconnect the "
              "other MIDI client) or expose another NT port, then retry." )


def addCheckSum( arr ):
	sum = 0
	for i in range( 7, len(arr) ):
		sum += arr[i]
	sum = ( -sum ) & 0x7f
	arr.append( sum )


def getCurrentPresetPath( ):
    arr = [ 0xF0, 0x00, 0x21, 0x27, 0x6D, sysExId, 0x56, 0xF7 ]
    outMsg = mido.Message.from_bytes( arr )
    outPort.send( outMsg )

    inMsg = inPort.receive()
    stringBytes = bytes(inMsg.data[6:])
    presetName = stringBytes.decode('ascii').split('\x00', 1)[0]
    return presetName


def newPreset( ):
    arr = [ 0xF0, 0x00, 0x21, 0x27, 0x6D, sysExId, 0x35, 0xF7 ]
    outMsg = mido.Message.from_bytes( arr )
    outPort.send(outMsg)


def setPresetName( name ):
    arr = [ 0xF0, 0x00, 0x21, 0x27, 0x6D, sysExId, 0x47 ]
    for i in range( len(name) ):
        arr.append( ord( name[ i ] ) )
    arr.append( 0 )
    arr.append( 0xF7 )
    outMsg = mido.Message.from_bytes( arr )
    outPort.send(outMsg)


def savePreset( ):
    arr = [ 0xF0, 0x00, 0x21, 0x27, 0x6D, sysExId, 0x36, 0x02, 0xF7 ]
    outMsg = mido.Message.from_bytes( arr )
    outPort.send(outMsg)
    # give the NT time to save the preset, as there is no ACK for this command
    # without this delay, getCurrentPresetPath will return the old preset name immediately after savePreset
    # if/when os decides to implement responses for preset save, this can be revisited, but for now a short sleep seems to work fine
    time.sleep( 0.5 )


# this method lifted mostly verbatim from file_send.py
def uploadFile( local_file, nt_path ):
    kOpUpload = 4
    ack = mido.Message.from_bytes( [ 0xF0, 0x00, 0x21, 0x27, 0x6D, sysExId, 0x7A, 0, kOpUpload, 0xF7 ] )

    with open( local_file, 'rb' ) as F:
	    data = F.read()
	
    size = len(data)
    uploadPos = 0

    while True:
        count = min( 512, size - uploadPos )
        if count == 0:
            break

        createAlways = int( uploadPos == 0 )

        arr = [ 0xF0, 0x00, 0x21, 0x27, 0x6D, sysExId, 0x7A, kOpUpload ]
        for i in range( len(nt_path) ):
            arr.append( ord( nt_path[ i ] ) )
        arr.append( 0 )
        arr.append( createAlways )
        arr.append( 0 )# ( uploadPos >> 63 ) & 0x7f )
        arr.append( 0 )# ( uploadPos >> 56 ) & 0x7f )
        arr.append( 0 )# ( uploadPos >> 49 ) & 0x7f )
        arr.append( 0 )# ( uploadPos >> 42 ) & 0x7f )
        arr.append( 0 )# ( uploadPos >> 35 ) & 0x7f )
        arr.append( ( uploadPos >> 28 ) & 0x0f )# ( uploadPos >> 28 ) & 0x7f )
        arr.append( ( uploadPos >> 21 ) & 0x7f )
        arr.append( ( uploadPos >> 14 ) & 0x7f )
        arr.append( ( uploadPos >> 7 ) & 0x7f )
        arr.append( ( uploadPos >> 0 ) & 0x7f )
        arr.append( 0 )# ( count >> 63 ) & 0x7f )
        arr.append( 0 )# ( count >> 56 ) & 0x7f )
        arr.append( 0 )# ( count >> 49 ) & 0x7f )
        arr.append( 0 )# ( count >> 42 ) & 0x7f )
        arr.append( 0 )# ( count >> 35 ) & 0x7f )
        arr.append( ( count >> 28 ) & 0x0f )# ( count >> 28 ) & 0x7f )
        arr.append( ( count >> 21 ) & 0x7f )
        arr.append( ( count >> 14 ) & 0x7f )
        arr.append( ( count >> 7 ) & 0x7f )
        arr.append( ( count >> 0 ) & 0x7f )
        for j in range(count):
            b = data[ uploadPos + j ]
            arr.append( ( b >> 4 ) & 0xf )
            arr.append( ( b ) & 0xf )

        addCheckSum( arr )
        arr.append( 0xF7 )
        outMsg = mido.Message.from_bytes( arr )
        outPort.send( outMsg )

        uploadPos += count
        
        inMsg = inPort.receive()
        if inMsg != ack:
            return False

    return True


def rescanPlugins( ):
    arr = [ 0xF0, 0x00, 0x21, 0x27, 0x6D, sysExId, 0x7A, 0x08 ]
    addCheckSum(arr)
    arr.append( 0xF7 )
    outMsg = mido.Message.from_bytes( arr )
    outPort.send(outMsg)


def loadPreset( nt_path ):
    arr = [ 0xF0, 0x00, 0x21, 0x27, 0x6D, sysExId, 0x34, 0x00 ]
    for i in range( len(nt_path) ):
        arr.append( ord( nt_path[ i ] ) )
    arr.append( 0x00 )
    arr.append( 0xF7 )
    outMsg = mido.Message.from_bytes( arr )
    outPort.send(outMsg)


# if a temp preset name was provided, first save current state to that preset
if (tempPresetName != None):
    setPresetName( tempPresetName )
    savePreset( )

# remember which preset is loaded
currentPreset = getCurrentPresetPath().strip()

# create a blank preset to allow plugins to reload
newPreset()

# upload the new plugin
fileCopied = uploadFile ( local_plugin_path, nt_plugin_path )
if fileCopied:
    # scan plugins to make the new one available
    rescanPlugins()

    # reload previous preset, if there was one
    if currentPreset != "":
        loadPreset( currentPreset )

    print( "Success!" )
else:
    print( "Error uploading plug-in file!" )
