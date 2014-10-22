'Functions to generate OTAP images'

import sys
import os
import struct
import commands

from lzss import LzssCompressor
from optparse import OptionParser

# the crypto directory is a sibling, make sure we can import from it
sys.path += [ os.path.join(os.path.split(__file__)[0], 'cryptopy') ]

from crypto.cipher.aes_cbc import AES_CBC
from crypto.cipher.base import padWithZeros


OTAP_KEY = '\xc3\xbd\x8f\x3c\xc7\xc9\x99\x29\x22\x92\xf3\xf2\xa2\x9d\xc3\x10'
OTAP_NONCE = '\x00' * 16


def generate_mic(data):
    '''Generate the MIC over the given data'''
    alg = AES_CBC(key=OTAP_KEY, keySize=len(OTAP_KEY), padding=padWithZeros())
    outdata = alg.encrypt(data, iv=OTAP_NONCE)
    # use the first 4 bytes of the CBC-MAC result as the MIC (per CCM spec)
    return struct.unpack('!L', outdata[-16:-12])[0]


# if bin file starts with executable partition header, remove it
# save output file with added .strip extension
def strip_exe_hdr(binfile, outfile = ''):
    '''Strip the executable partition header from .bin file'''
    EXEC_PAR_HDR_SIGNATURE = '\xfe\xff\xff\xff\x78\x78'
    EXEC_PAR_HDR_LEN = 32

    inf = open(binfile, 'rb')
    file_data = inf.read()
    inf.close()

    if file_data[4:10] == EXEC_PAR_HDR_SIGNATURE:
       #print 'stripping exe hdr'
       # if exec partition header is at start of data, strip it off
       file_data = file_data[EXEC_PAR_HDR_LEN:]

    if not outfile:
       outfile = binfile + '.strip'
       
    outf = open(outfile, 'wb')
    outf.write(file_data)
    outf.close()


def otap_file_action(target, source, env):
    '''
    Generate an OTAP file from a binary image
    env must include:
    PARTITION_ID
    START_ADDR
    APP_ID
    VENDOR_ID
    HARDWARE_ID
    VERSION
    DEPENDS_VERSION
    '''

    # remove exe partition header if present
    strip_exe_hdr(source[0].path)
    
    return make_otap_file(source[0].path + '.strip', target[0].path,
                   partition_id=env['PARTITION_ID'],
                   start_addr=env['START_ADDR'],
                   app_id=env['APP_ID'],
                   vendor_id=env['VENDOR_ID'],
                   hardware_id=env['HARDWARE_ID'],
                   version=env['VERSION'], # version is an array
                   depends_version=env['DEPENDS_VERSION'], # depends version is an array
                   )
    


def make_otap_file(infile, outfile,
                   is_executable=True, do_compression=True, skip_validation=False,
                   **kwargs):
    """Create an OTAP file with the specified parameters

    Keyword parameters:
    partition_id
    start_addr
    app_id
    vendor_id
    hardware_id
    version
    depends_version

    Optional parameters:
    executable - mark as an executable image (defaults to True)
    compress - compress the file data (defaults to True)
    skip_validation - tell the receiver to skip validation (defaults to False)
    """
    # First, generate the file data (without the MIC)
    # Data
    inf = open(infile, 'rb')
    file_data = inf.read()
    inf.close()

    # Figure out the Flags
    #is_executable = True # default
    #if kwargs contains 'compress':
    #    is_executable = bool(kwargs['executable'])
    
    #do_compression = True # default
    #if kwargs contains 'compress':
    #    do_compression = bool(kwargs['compress'])

    #skip_validation = False # default
    #if kwargs contains 'compress':
    #    skip_validation = bool(kwargs['skip_validation'])
        
    # Compress the file data
    if do_compression:
        compressor = LzssCompressor()  # TODO: params
        compressed_data = compressor.compress(file_data)
    else:
        compressed_data = file_data
    
    # pad length to 4-byte boundary
    if (len(compressed_data) % 4):
       compressed_data += '\x00' * (4 - len(compressed_data) % 4)

    # CHANGE ME if the OTAP File Info structure changes
    HEADER_LEN = 24
    
    file_header = ''
    # Size
    file_header += struct.pack('!L', len(compressed_data) + HEADER_LEN)
    # -- File Info
    # Partition Id
    file_header += struct.pack('!B', kwargs['partition_id'])
    # Flags :
    #  0: exe (1) or partition (0),
    #  1: compressed? (yes = 1),
    #  2: skip validation? (yes = 1)
    flags = 0
    if is_executable:
        flags |= 1
    if do_compression:
        flags |= 2
    if skip_validation:
        flags |= 4
    file_header += struct.pack('!B', flags)
    
    # uncompressed size
    file_header += struct.pack('!L', len(file_data))
    # Addr
    file_header += struct.pack('!L', kwargs['start_addr'])
    # version
    file_header += struct.pack('!BBBH', *kwargs['version'])
    # depends version
    file_header += struct.pack('!BBBH', *kwargs['depends_version'])
    # app id
    file_header += struct.pack('!B', kwargs['app_id'])
    # vendor id
    file_header += struct.pack('!H', kwargs['vendor_id'])
    # hardware id
    file_header += struct.pack('!B', kwargs['hardware_id'])

    # Open the output file
    f = open(outfile, 'wb')
    # generate the CRC (or MIC) over the (compressed) file data
    f.write(struct.pack('!L', generate_mic(file_header + compressed_data)))
    # write the file data
    f.write(file_header)
    f.write(compressed_data)
    f.close()

    return 0

def parse_args():
    'Parse the command line'
    parser = OptionParser("usage: %prog [options]", version="%prog v1.0") # TODO
    parser.add_option("--verbose", dest="verbose", 
                        default=False, action="store_true",
                        help="Display verbose output")
    parser.add_option("--all", dest="all_files", 
                        default=False, action="store_true",
                        help="Generate all example files")
    parser.add_option("-o", "--output", dest="output_file", 
                        default='image',
                        help="Output file")
    parser.add_option("-i", "--input", dest="input_file", 
                        default='data.in',
                        help="Input file")
    parser.add_option("--no-compression", dest="do_compression", 
                        default=True, action="store_false",
                        help="Don't compress the image data")
    parser.add_option("-p", "--partition", dest="partition_id", 
                        default=1, type='int',
                        help="Partition Id")
    parser.add_option("--start-addr", dest="start_addr", 
                        default=0, type='int',
                        help="Start address (for executable partitions)")
    parser.add_option("--app-id", dest="app_id", 
                        default=1, type='int',
                        help="App Id (for executable partitions)")
    parser.add_option("--vendor-id", dest="vendor_id", 
                        default=1, type='int',
                        help="Vendor Id (for executable partitions)")
    parser.add_option("--hardware-id", dest="hardware_id", 
                        default=1, type='int',
                        help="Hardware Id (for executable partitions)")
    parser.add_option("--exe-version", dest="version", 
                        default='1.0.0.0',
                        help="Version (for executable partitions)")
    parser.add_option("--depends-version", dest="depends_version", 
                        default='0.0.0.0', # all 0s means we don't care
                        help="Depends version (for executable partitions)")
    parser.add_option("--not-executable", dest="is_executable", 
                        default=True, action="store_false",
                        help="Create non-executable image")
    parser.add_option("--skip-validation", dest="skip_validation", 
                        default=False, action="store_true",
                        help="Skip validation")
    
    (opts, args) = parser.parse_args()

    return opts


def main():
    opts = parse_args()
    kwargs = opts.__dict__
    # transform the versions
    kwargs['version'] = [int(i) for i in kwargs['version'].split('.')]
    kwargs['depends_version'] = [int(i) for i in kwargs['depends_version'].split('.')]
    # make sure the output file extension is .otap2
    output_file = opts.output_file
    if os.path.splitext(output_file)[1] != '.otap2':
        output_file += '.otap2'
    if opts.verbose:
        print 'Creating %s with data:' % opts.output_file, kwargs
    make_otap_file(opts.input_file, output_file, **kwargs)

if __name__ == '__main__':
    main()