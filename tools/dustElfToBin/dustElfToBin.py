import os
import sys
import struct
import io
from subprocess import Popen, PIPE, STDOUT

# CRC generation functions borrowed from OskiImageTools
def gen_bits(byte):
    'Return a list of the bits in this byte'
    return [(byte >> shift) & 1 for shift in range(8)]

def dust_crc(int_list):
    'Return the Dust BOPS CRC from data buffer as a list of bytes'
    c = [0] * 16
    new_crc = [0] * 16
    
    for byte in int_list:
        d = gen_bits(byte)
        new_crc[0] = (d[4] + d[0] + c[8] + c[12]) % 2
        new_crc[1] = (d[5] + d[1] + c[9] + c[13]) % 2
        new_crc[2] = (d[6] + d[2] + c[10] + c[14]) % 2
        new_crc[3] = (d[7] + d[3] + c[11] + c[15]) % 2
        new_crc[4] = (d[4] + c[12]) % 2
        new_crc[5] = (d[5] + d[4] + d[0] + c[8] + c[12] + c[13]) % 2
        new_crc[6] = (d[6] + d[5] + d[1] + c[9] + c[13] + c[14]) % 2
        new_crc[7] = (d[7] + d[6] + d[2] + c[10] + c[14] + c[15]) % 2
        new_crc[8] = (d[7] + d[3] + c[0] + c[11] + c[15]) % 2
        new_crc[9] = (d[4] + c[1] + c[12]) % 2
        new_crc[10] = (d[5] + c[2] + c[13]) % 2
        new_crc[11] = (d[6] + c[3] + c[14]) % 2
        new_crc[12] = (d[7] + d[4] + d[0] + c[4] + c[8] + c[12] + c[15]) % 2
        new_crc[13] = (d[5] + d[1] + c[5] + c[9] + c[13]) % 2
        new_crc[14] = (d[6] + d[2] + c[6] + c[10] + c[14]) % 2
        new_crc[15] = (d[7] + d[3] + c[7] + c[11] + c[15]) % 2
        #temp = d[7] + d[3] + c[7] + c[11] + c[15]
        c = new_crc[:]  # make a copy
        
    crc_as_int = sum(c[idx] << idx for idx in range(16))
    return crc_as_int

# fn to get the length of the data
def get_length_from_offset (fileName, offset):
   with io.open(fileName, 'r+b') as fd:
      # move the file pointer to the end of the file
      fd.seek(0, os.SEEK_END)
      # get the len
      length = (fd.tell() - offset)
   return length

# fn to get checksum for a section in bin file
def get_crc_from_offset (fileName, offset):
   with io.open(fileName, 'r+b') as fd:
      # move the file pointer to the offset
      fd.seek(offset, 0)
      # read the data from the file
      msg = fd.read()
      # get crc for the data
      checksum = dust_crc([ord(b) for b in msg])
   return checksum

# fn to write list of data into particular location in elf file
def write_data_to_file (fileName, offset, data):
   # open the target file
   with io.open(fileName, 'r+b') as fd:
      # move file pointer to the offset location
      fd.seek(offset, 0)
      fd.write(data)
   return

# fn to write checksum and length data into an out file
def write_checksum_length_into_file (binFile, elfFile):
   # constants
   KERNEL_HDR_SIZE = 32
   CHECKSUM_OFFSET_IN_KERNEL_HDR = 8
   LENGTH_OFFSET_IN_KERNEL_HDR = 10
   BYTE_ORDER_OFFSET_IN_OUTFILE = 5
   KERNEL_HDR_LOC_IN_ELF_FILE = 52
   
   # get the checksum starting after kernel header length field till the end of the file
   length = get_length_from_offset(binFile, LENGTH_OFFSET_IN_KERNEL_HDR + 4)
   length = struct.pack('<I', length)
   write_data_to_file(binFile, LENGTH_OFFSET_IN_KERNEL_HDR, length)
   checksum = get_crc_from_offset(binFile, LENGTH_OFFSET_IN_KERNEL_HDR)
   # change the byte order
   checksum = struct.pack('<H', checksum)
   data = ''+checksum+''+length
   # offset of the crc in the elf file
   print 'Converting ELF to bin: %s -> %s, with checksum and length' % (elfFile, binFile)
   write_data_to_file(elfFile, KERNEL_HDR_LOC_IN_ELF_FILE + CHECKSUM_OFFSET_IN_KERNEL_HDR, data)
   return 0

   # fn to convert elf to bin file
def elf_to_bin (binName, elfName):
   status = 0
   output = ''
   toolVer = "6.40.1"
   try:
      toolBase = os.environ['IAR_ARM_BASE']
   except KeyError:
      toolBase = os.path.join ('/Program Files','IAR Systems', 'EW_ARM_'+toolVer) # default
   
   elfToBin = os.path.join(toolBase, 'arm','bin','ielftool.exe')
   execCmd = [elfToBin,'--bin', '--verbose', elfName, binName]
   if os.name in ['nt', 'win32']:
      p = Popen(execCmd, shell=False, stdin=None, stdout=PIPE, stderr=STDOUT)
   else:
      # shell=False means python will handle the quoting for us
      p = Popen(execCmd, shell=False, stdin=None, stdout=PIPE, stderr=STDOUT, close_fds=True)
   output = p.communicate()[0]
   status = p.returncode
   return status, output

# main program
def main(input, target):
   # generate a temp bin file
   if not target:
      tempFile = os.path.splitext(input)[0] + '.tmp'
      target = os.path.splitext(input)[0] + '.bin'
   else:
      tempFile = os.path.splitext(target)[0] + '.tmp'
   elf_to_bin(tempFile, input)
   # generate and add crc and length from temp bin file to elf file
   write_checksum_length_into_file(tempFile, input)
   # remove temp bin file
   os.remove(tempFile)
   # generate bin file from the elf file
   (status, output) = elf_to_bin(target, input)
   print output
   return status

if __name__ == '__main__':
   binFile = ''
   if len(sys.argv) == 1:
      print 'usage:\n\r%s <inputElfFile> <(optional)BinFile>' % (sys.argv[0])
      sys.exit(1)
   elif len(sys.argv) > 2:
      binFile = sys.argv[2]
   main(sys.argv[1], binFile)
