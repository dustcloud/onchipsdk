'''
\copyright Copyright (c) 2013, Dust Networks.  All rights reserved.

Python module to create an OTAP file, either as a standalone app, or as part
of an SCons build system.

To run it as a standalone application, enter the following command:

python OtapFile.py -i app.bin -o app.otap2 --partition 2 --app-id 2 --start-addr 0x41020 --hardware-id 3
'''

import sys
import os
import struct
import commands

from lzss import LzssCompressor
from optparse import OptionParser

# the crypto directory is a sibling, make sure we can import from it
sys.path += [ os.path.join(os.path.split(__file__)[0], 'cryptopy') ]

from crypto.cipher.aes import AES
from crypto.cipher.ccm import CCM

#============================ defines =========================================

## Key to calculate the MIC of the OTAP file
OTAP_KEY                = '\xa7\x52\xb3\x4f\x88\xd1\x18\x0c\xee\x19\x94\x70\x6d\xf3\x2c\x55'

## Nonce to calculate the MIC of the OTAP file
OTAP_NONCE              = '\x3d\x78\x5a\x03\xcc\x18\xf9\xa2\x60\xb2\xe1\x44\x9b'

## Length of the OTAP MIC
OTAP_MIC_LEN = 4

## Extension of an OTAP file
OTAP_FILE_EXTENSION     = '.otap2'

## Signature of a kernel header
KERNEL_HEADER_SIGNATURE = 'EXE1\xfe\xff\xff\xff'

## Length of a kernel header
KERNEL_HEADER_LEN       = 32

## Length of the OTAP header (does not include size field nor MIC)
OTAP_HEADER_LEN         = 24

## default values in command line
DFLT_INPUT_FILE         = 'data.bin'
DFLT_OUTPUT_FILE        = 'image'
DFLT_HARDWARE_ID        = 3            # 3 == Eterna
DFLT_VENDOR_ID          = 1            # 1 == Dust Networks
DFLT_APP_ID             = 20
DFLT_VERSION            = '1.0.0.0'
DFLT_DEPENDS_VERSION    = '0.0.0.0'    # all 0's == don't care
DFLT_PARTITION_ID       = 2            # 2 == main executable partition
DFLT_START_ADDR         = 0x41020      # 0x41020 == address executable with default partition table

#============================ helpers =========================================

def make_otap_file(infile, outfile,
                   is_executable=True, do_compression=True, skip_validation=False,
                   **kwargs):
    '''
    \brief Create an OTAP file with the specified parameters.
    
    \param[in] infile             See description of
        <tt>--input</tt> command line parameter.
    \param[in] outfile            See description of
        <tt>--output</tt> command line parameter.
    \param[in] is_executable      See description of
        <tt>--executable</tt> command line parameter.
    \param[in] do_compression     See description of
        <tt>--do-compression</tt> command line parameter.
    \param[in] skip_validation    See description of
        <tt>--skip-validation</tt> command line parameter.
    \param[in] kwargs             Mandatory keyword arguments:
        - <tt>partition_id</tt> (integer): See description of
           <tt>--partition</tt> command line parameter.
        - <tt>start_addr</tt> (integer): See description of
           <tt>--start-addr</tt> command line parameter.
        - <tt>app_id</tt> (integer): See description of
           <tt>--app-id</tt> command line parameter.
        - <tt>vendor_id</tt> (integer): See description of
           <tt>--vendor-id</tt> command line parameter.
        - <tt>hardware_id</tt> (integer): See description of
           <tt>--hardware-id</tt> command line parameter.
        - <tt>version</tt> (a list of 4 integers): See description of
           <tt>--exe-version</tt> command line parameter.
        - <tt>depends_version</tt> (a list of 4 integers): See description of
           <tt>--depends-version</tt> command line parameter.
    
    \return 0, always.
    '''
    
    verbose = kwargs.get('verbose',False)
    
    # print
    if verbose:
        output  = []
        output += ['']
        output += ['Creating OTAP file with the following parameters:']
        output += [' - {0:<20}: {1}'.format('infile',         infile)]
        output += [' - {0:<20}: {1}'.format('outfile',        outfile)]
        output += [' - {0:<20}: {1}'.format('is_executable',  is_executable)]
        output += [' - {0:<20}: {1}'.format('do_compression', do_compression)]
        output += [' - {0:<20}: {1}'.format('skip_validation',skip_validation)]
        output += [' - {0:<20}: {1}'.format(k,v) for (k,v) in kwargs.items()]
        output += ['']
        output  = '\n'.join(output)
        print output
    
    #===== step 0.
    
    if verbose:
        print 'Read input (binary) file {0}...'.format(infile)
    
    f = open(infile, 'rb')
    file_data = f.read()
    f.close()
    
    #===== step 1.
    
    if file_data.startswith(KERNEL_HEADER_SIGNATURE):
        if verbose:
            print 'Stripping kernel header...'
        file_data = file_data[KERNEL_HEADER_LEN:]
    else:
        if verbose:
            print 'No kernel header present'
    
    #===== step 2.
    
    if do_compression:
        if verbose:
            print 'Compressing data...'
        compressor      = LzssCompressor()  # TODO: params
        compressed_data = compressor.compress(file_data)
    else:
        compressed_data = file_data
    
    # pad length to 4-byte boundary
    if (len(compressed_data) % 4):
       compressed_data += '\x00' * (4 - len(compressed_data) % 4)
    
    #===== step 3.
    
    if verbose:
        print 'Creating OTAP header...'
    
    file_header    = ''
    #= [4B] size
    file_header   += struct.pack('!L',      len(compressed_data) + OTAP_HEADER_LEN)
    
    #= [1B] partition ID
    file_header   += struct.pack('!B',      kwargs['partition_id'])
    
    #= [1B] flags
    # bit  0:  exe (1) or partition (0),
    # bit  1:  compressed? (yes = 1),
    # bit  2:  skip validation? (yes = 1)
    # bits 3+: reserved. Set to 0.
    flags          = 0
    if is_executable:
        flags     |= 1
    if do_compression:
        flags     |= 2
    if skip_validation:
        flags     |= 4
    file_header   += struct.pack('!B',      flags)
    
    #= [4B] uncompressed size
    file_header   += struct.pack('!L',      len(file_data))
    
    #= [4B] start address
    file_header   += struct.pack('!L',      kwargs['start_addr'])
    
    #= [5B] version
    file_header   += struct.pack('!BBBH',  *kwargs['version'])
    
    #= [5B] depends version
    file_header   += struct.pack('!BBBH',  *kwargs['depends_version'])
    
    #= [1B] application ID
    file_header   += struct.pack('!B',      kwargs['app_id'])
    
    #= [2B] vendor ID
    file_header   += struct.pack('!H',      kwargs['vendor_id'])
    
    #= [1B] hardware ID
    file_header   += struct.pack('!B',      kwargs['hardware_id'])
    
    #===== step 4.
    
    if verbose:
        print 'Writing output (OTAP) file {0}...'.format(outfile)
    
    # The OTAP file contains: MIC(header+data) + header + data
    
    f = open(outfile, 'wb')
    f.write(struct.pack('!L', generate_mic(file_header + compressed_data)))
    f.write(file_header)
    f.write(compressed_data)
    f.close()
    
    if verbose:
        print 'done.'
    
    return 0

def generate_mic(data):
    '''
    \brief Generate a Message Integrity Code (MIC) over some given data.
    
    \param[in] data The data to calculate the MIC over, a binary string.
    
    \returns The 32-bit MIC, an integer.
    '''
    #alg       = AES_CBC(key=OTAP_KEY, keySize=len(OTAP_KEY), padding=padWithZeros())
    #outdata   = alg.encrypt(data, iv=OTAP_NONCE)
    alg = CCM(AES(OTAP_KEY), macSize=OTAP_MIC_LEN)
    outdata   = alg.encrypt('', OTAP_NONCE, addAuthData=data)
    
    # the 32-bit MIC is the first 4 bytes of the CBC-MAC result (per CCM spec)
    #return struct.unpack('!L', outdata[-16:-12])[0]
    return struct.unpack('!L', outdata[-OTAP_MIC_LEN:])[0]

#============================ Scons environment ===============================
'''
The following only applies when using this module as part of an SCons-based
build system (see http://www.scons.org/).
'''

def otap_file_action(target, source, env):
    '''
    SCons action to generate an OTAP file from a binary image.
    
    \note The <tt>env</tt> should contain the following keys:
    - <tt>IS_EXE_PARTITION</tt>
    - <tt>PARTITION_ID</tt>
    - <tt>START_ADDR</tt>
    - <tt>APP_ID</tt>
    - <tt>VENDOR_ID</tt>
    - <tt>HARDWARE_ID</tt>
    - <tt>VERSION</tt>
    - <tt>DEPENDS_VERSION</tt>
    
    Refer to the documentation of #make_otap_file() for a description of these
    parameters, including their expected format.
    '''
    
    return make_otap_file(
        source[0].path,
        target[0].path,
        is_executable   = env['IS_EXE_PARTITION'] if 'IS_EXE_PARTITION' in env else True,
        partition_id    = env['PARTITION_ID'],
        start_addr      = env['START_ADDR'],
        app_id          = env['APP_ID'],
        vendor_id       = env['VENDOR_ID'],
        hardware_id     = env['HARDWARE_ID'],
        version         = env['VERSION'], # version is an array
        depends_version = env['DEPENDS_VERSION'], # depends version is an array
    )

#============================ standalone application ==========================
'''
The following only applies when calling this script as a standalone application
'''

def parse_args():
    '''
    \brief Parse the command line argument.
    
    \returns A dictionary containing the command line arguments in the right
        format (e.g. integer), or default values.
    '''
    parser = OptionParser(
        description = 'Application to create OTAP files. Copyright (c) 2013, Dust Networks.  All rights reserved.',
        usage      = 'usage: %prog [options]',
    )
    
    # admin
    parser.add_option(  '--verbose',
        dest       = 'verbose', 
        default    = False,
        action     = 'store_true',
        help       = 'display verbose output',
    )
    
    # input/ouput
    parser.add_option(  '-i', '--input',
        dest       = 'input_file', 
        default    = DFLT_INPUT_FILE,
        help       = 'path to the input file',
    )
    parser.add_option(  '-o', '--output',
        dest       = 'output_file', 
        default    = DFLT_OUTPUT_FILE,
        help       = 'path to the (OTAP) output file',
    )
    
    # flags
    parser.add_option(  '--not-executable',
        dest       = 'is_executable', 
        default    = True,
        action     = 'store_false',
        help       = 'mark the image as not executable',
    )
    parser.add_option(  '--no-compression',
        dest       = 'do_compression', 
        default    = True,
        action     = 'store_false',
        help       = 'do not compress the file data',
    )
    parser.add_option(  '--skip-validation',
        dest       = 'skip_validation', 
        default    = False,
        action     = 'store_true',
        help       = 'set a flag in the OTAP file\'s header to tell the target device to skip validation of the app ID and vendor ID',
    )
    
    # IDs and versions
    parser.add_option(  '--hardware-id',
        dest       = 'hardware_id', 
        default    = DFLT_HARDWARE_ID,
        type       = 'int',
        help       = '[executable images only] ID of the hardware the application in the OTAP file should run on',
    )
    parser.add_option(  '--vendor-id',
        dest       = 'vendor_id', 
        default    = DFLT_VENDOR_ID,
        type       = 'int',
        help       = '[executable images only] vendor ID of the application contained in the OTAP file',
    )
    parser.add_option(  '--app-id',
        dest       = 'app_id', 
        default    = DFLT_APP_ID,
        type       = 'int',
        help       = '[executable images only] ID of the application contained in the OTAP file',
    )
    parser.add_option(  '--exe-version',
        dest       = 'version', 
        default    = DFLT_VERSION,
        help       = '[executable images only] version of the application contained in the OTAP file',
    )
    parser.add_option(  '--depends-version',
        dest       = 'depends_version', 
        default    = DFLT_DEPENDS_VERSION,   
        help       = '[executable images only] minimal version that needs to be running on the target device for this OTAP to be accepted during the OTAP handshake',
    )
    
    # memory location
    parser.add_option(  '-p', '--partition',
        dest       = 'partition_id',
        default    = DFLT_PARTITION_ID,
        type       = 'int',
        help       = 'ID of the partition on the target device to write into',
    )
    parser.add_option(  '--start-addr',
        dest       = 'start_addr',
        default    = DFLT_START_ADDR,
        type       = 'int',
        help       = 'absolute memory address in the target device to write to. In case this is an executable image, the target device prepends a {0}-byte kernel header when the OTAP operation is successful. When OTAP\'ing an executable image, the value of this parameter should therefore point to {0} bytes after the beginnning of the executable partition'.format(KERNEL_HEADER_LEN),
    )
    
    (opts, args)   = parser.parse_args()
    
    return opts

def main():
    
    # gather command line options
    opts           = parse_args()
    kwargs         = opts.__dict__
    
    # transform the versions, e.g: '1.2.3.4' -> [1,2,3,4]
    kwargs['version']         = [int(i) for i in kwargs['version'].split('.')]
    kwargs['depends_version'] = [int(i) for i in kwargs['depends_version'].split('.')]
    
    # make sure the output file extension is correct
    output_file = opts.output_file
    if os.path.splitext(output_file)[1]!=OTAP_FILE_EXTENSION:
        output_file+=OTAP_FILE_EXTENSION
    
    # generate the OTAP file
    make_otap_file(opts.input_file, output_file, **kwargs)

if __name__ == '__main__':
    main()