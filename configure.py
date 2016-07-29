#! /usr/bin/env python
#---------------------------------------------------------------------------------------
# configure.py: Athena++ configuration script in python. Original version by CJW.
#
# When configure.py is run, it uses the command line options and default settings to
# create custom versions of the files Makefile and src/defs.hpp from the template files
# Makefile.in and src/defs.hpp.in repspectively.
#
# The following options are implememted:
#   -h  --help        help message
#   --prob=name       use src/pgen/name.cpp as the problem generator
#   --coord=choice    use choice as the coordinate system
#   --eos=choice      use choice as the equation of state
#   --flux=choice     use choice as the Riemann solver
#   --order=choice    use choice as the spatial reconstruction algorithm
#   --fint=choice     use choice as the hydro time-integration algorithm
#   -b                enable magnetic fields
#   -s                enable special relativity
#   -g                enable general relativity
#   -t                enable interface frame transformations for GR
#   -pp               enable post-processing
#   --cxx=choice      use choice as the C++ compiler
#   --std=choice      use choice as the C++ standard
#   --chemistry=choice enable chemistry, use choice as chemical network
#   --cvode_path=path  path to CVODE libraries (chemistry requires the cvode library)
#   -debug            enable debug flags (-g -O0); override other compiler options
#   -mpi              enable parallelization with MPI
#   -omp              enable parallelization with OpenMP
#   -hdf5             enable HDF5 output (requires the HDF5 library)
#   --hdf5_path=path  path to HDF5 libraries (requires the HDF5 library)
#   --ifov=N          enable N internal hydro output variables
#   -radiation        enable radiative transfer
#---------------------------------------------------------------------------------------

# Modules
import argparse
import glob
import re

# Set template and output filenames
makefile_input = 'Makefile.in'
makefile_output = 'Makefile'
defsfile_input = 'src/defs.hpp.in'
defsfile_output = 'src/defs.hpp'

#--- Step 1. Prepare parser, add each of the arguments ---------------------------------
parser = argparse.ArgumentParser()

# --prob=[name] argument
pgen_directory = 'src/pgen/'
# set pgen_choices to list of .cpp files in src/pgen/
pgen_choices = glob.glob(pgen_directory + '*.cpp')
# remove 'src/pgen/' prefix and '.cpp' extension from each filename
pgen_choices = [choice[len(pgen_directory):-4] for choice in pgen_choices]
parser.add_argument('--prob',
    default='shock_tube',
    choices=pgen_choices,
    help='select problem generator')

# --coord=[name] argument
parser.add_argument('--coord',
    default='cartesian',
    choices=['cartesian','cylindrical','spherical_polar','minkowski','sinusoidal',
        'tilted','schwarzschild','kerr-schild'],
    help='select coordinate system')

# --eos=[name] argument
parser.add_argument('--eos',
    default='adiabatic',
    choices=['adiabatic','isothermal'],
    help='select equation of state')

# --flux=[name] argument
parser.add_argument('--flux',
    default='default',
    choices=['default','hlle','hllc','hlld','roe','llf'],
    help='select Riemann solver')

# --order=[name] argument
parser.add_argument('--order',
    default='plm',
    choices=['plm'],
    help='select spatial reconstruction algorithm')

# --fint=[name] argument
parser.add_argument('--fint',
    default='vl2',
    choices=['vl2'],
    help='select hydro time-integration algorithm')

# -b argument
parser.add_argument('-b',
    action='store_true',
    default=False,
    help='enable magnetic field')

# -s argument
parser.add_argument('-s',
    action='store_true',
    default=False,
    help='enable special relativity')

# -g argument
parser.add_argument('-g',
    action='store_true',
    default=False,
    help='enable general relativity')

# -t argument
parser.add_argument('-t',
    action='store_true',
    default=False,
    help='enable interface frame transformations for GR')

# --chemistry argument
parser.add_argument('--chemistry',
    default=None,
    choices=["gow16", "gow16_ng"],
    help='select chemical network')

# --cvode_path argument
parser.add_argument('--cvode_path',
    type=str,
    default='',
    help='path to CVODE libraries')

# -pp argument
parser.add_argument('-pp',
    action='store_true',
    default=False,
    help='enable post-processing')

# --cxx=[name] argument
parser.add_argument('--cxx',
    default='g++',
    choices=['g++','icc','cray','bgxl'],
    help='select C++ compiler')

# --std=[name] argument
parser.add_argument('--std',
    default=None,
    choices=["c++11"],
    help='select C++ standard')

# -debug argument
parser.add_argument('-debug',
    action='store_true',
    default=False,
    help='enable debug flags; override other compiler options')

# -mpi argument
parser.add_argument('-mpi',
    action='store_true',
    default=False,
    help='enable parallelization with MPI')

# -omp argument
parser.add_argument('-omp',
    action='store_true',
    default=False,
    help='enable parallelization with OpenMP')

# -hdf5 argument
parser.add_argument('-hdf5',
    action='store_true',
    default=False,
    help='enable HDF5 Output')

# --hdf5_path argument
parser.add_argument('--hdf5_path',
    type=str,
    default='',
    help='path to HDF5 libraries')

# -ifov=N argument
parser.add_argument('--ifov',
    type=int,
    default=0,
    help='number of internal hydro output variables')

# -radiation argument
parser.add_argument('-radiation',
    action='store_true',
    default=False,
    help='enable radiative transfer')

# Parse command-line inputs
args = vars(parser.parse_args())

#--- Step 2. Test for incompatible arguments -------------------------------------------

# Set default flux; HLLD for MHD, HLLC for hydro, HLLE for isothermal hydro or any GR
if args['flux'] == 'default':
  if args['g']:
    args['flux'] = 'hlle'
  elif args['b']:
    args['flux'] = 'hlld'
  elif args['eos'] == 'isothermal':
    args['flux'] = 'hlle'
  else:
    args['flux'] = 'hllc'

# Check Riemann solver compatibility
if args['flux'] == 'hllc' and args['eos'] == 'isothermal':
  raise SystemExit('### CONFIGURE ERROR: HLLC flux cannot be used with isothermal EOS')
if args['flux'] == 'hllc' and args['b']:
  raise SystemExit('### CONFIGURE ERROR: HLLC flux cannot be used with MHD')
if args['flux'] == 'hlld' and not args['b']:
  raise SystemExit('### CONFIGURE ERROR: HLLD flux can only be used with MHD')

# Check relativity
if args['s'] and args['g']:
  raise SystemExit('### CONFIGURE ERROR: ' \
      + 'GR implies SR; the -s option is restricted to pure SR.')
if args['t'] and not args['g']:
  raise SystemExit('### CONFIGURE ERROR: Frame transformations only apply to GR.')
if args['g'] and args['coord'] in ('cartesian','cylindrical','spherical_polar'):
  raise SystemExit('### CONFIGURE ERROR: ' \
      + 'GR cannot be used with ' + args['coord'] + ' coordinates')
if not args['g'] and args['coord'] not in ('cartesian','cylindrical','spherical_polar'):
  raise SystemExit('### CONFIGURE ERROR: ' \
      + args['coord'] + ' coordinates only apply to GR')
if args['eos'] == 'isothermal':
  if args['s'] or args['g']:
    raise SystemExit('### CONFIGURE ERROR: '\
        + 'Isothermal EOS is incompatible with relativity.')

#--- Step 3. Set definitions and Makefile options based on above arguments -------------

# Prepare dictionaries of substitutions to be made
definitions = {}
makefile_options = {}
makefile_options['LOADER_FLAGS'] = ''

# --prob=[name] argument
definitions['PROBLEM'] = makefile_options['PROBLEM_FILE'] = args['prob']

# --coord=[name] argument
definitions['COORDINATE_SYSTEM'] = makefile_options['COORDINATES_FILE'] = args['coord']

# --eos=[name] argument
definitions['NON_BAROTROPIC_EOS'] = '1' if args['eos'] == 'adiabatic' else '0'
makefile_options['EOS_FILE'] = args['eos']
# set number of hydro variables for adiabatic/isothermal
if args['eos'] == 'adiabatic':
  definitions['NHYDRO_VARIABLES'] = '5'
if args['eos'] == 'isothermal':
  definitions['NHYDRO_VARIABLES'] = '4'

# --flux=[name] argument
definitions['RSOLVER'] = makefile_options['RSOLVER_FILE'] = args['flux']

# --order=[name] argument
definitions['RECONSTRUCT'] = makefile_options['RECONSTRUCT_FILE'] = args['order']

# --fint=[name] argument
definitions['HYDRO_INTEGRATOR'] = makefile_options['HYDRO_INT_FILE'] = args['fint']

# -b argument
# set variety of macros based on whether MHD/hydro or adi/iso are defined
if args['b']:
  definitions['MAGNETIC_FIELDS_ENABLED'] = '1'
  makefile_options['EOS_FILE'] += '_mhd'
  definitions['NFIELD_VARIABLES'] = '3'
  makefile_options['RSOLVER_DIR'] = 'mhd/'
  if args['flux'] == 'hlle' or args['flux'] == 'llf' or args['flux'] == 'roe':
    makefile_options['RSOLVER_FILE'] += '_mhd'
  if args['eos'] == 'adiabatic':
    definitions['NWAVE_VALUE'] = '7'
  else:
    definitions['NWAVE_VALUE'] = '6'
    if args['flux'] == 'hlld':
      makefile_options['RSOLVER_FILE'] += '_iso'
else:
  definitions['MAGNETIC_FIELDS_ENABLED'] = '0'
  makefile_options['EOS_FILE'] += '_hydro'
  definitions['NFIELD_VARIABLES'] = '0'
  makefile_options['RSOLVER_DIR'] = 'hydro/'
  if args['eos'] == 'adiabatic':
    definitions['NWAVE_VALUE'] = '5'
  else:
    definitions['NWAVE_VALUE'] = '4'

# -s, -g, and -t arguments
definitions['RELATIVISTIC_DYNAMICS'] = '1' if args['s'] or args['g'] else '0'
definitions['GENERAL_RELATIVITY'] = '1' if args['g'] else '0'
if args['s']:
  makefile_options['EOS_FILE'] += '_sr'
  makefile_options['RSOLVER_FILE'] += '_rel'
if args['g']:
  makefile_options['EOS_FILE'] += '_gr'
  makefile_options['RSOLVER_FILE'] += '_rel'
  if not args['t']:
    makefile_options['RSOLVER_FILE'] += '_no_transform'

# -pp argument
if args['pp']:
  definitions['POST_PROCESSING_ENABLED'] = '1'
else:
  definitions['POST_PROCESSING_ENABLED'] = '0'

# -radiation argument
if args['radiation']:
  definitions['RADIATION_ENABLED'] = '1'
  makefile_options['RADIATION_FILE'] = '*.cpp'
else:
  definitions['RADIATION_ENABLED'] = '0'
  makefile_options['RADIATION_FILE'] = '*.cpp'

# --cxx=[name] argument
if args['cxx'] == 'g++':
  definitions['COMPILER_CHOICE'] = makefile_options['COMPILER_CHOICE'] = 'g++'
  makefile_options['PREPROCESSOR_FLAGS'] = ''
  makefile_options['COMPILER_FLAGS'] = '-O3'
  makefile_options['LINKER_FLAGS'] = ''
  makefile_options['LIBRARY_FLAGS'] = ''
if args['cxx'] == 'icc':
  definitions['COMPILER_CHOICE'] = makefile_options['COMPILER_CHOICE'] = 'icc'
  makefile_options['PREPROCESSOR_FLAGS'] = ''
  makefile_options['COMPILER_FLAGS'] = '-O3 -xhost -ipo -inline-forceinline'
  makefile_options['LINKER_FLAGS'] = ''
  makefile_options['LIBRARY_FLAGS'] = ''
if args['cxx'] == 'cray':
  definitions['COMPILER_CHOICE'] = 'cray'
  makefile_options['COMPILER_CHOICE'] = 'CC'
  makefile_options['PREPROCESSOR_FLAGS'] = ''
  makefile_options['COMPILER_FLAGS'] = '-O3 -h aggress -h vector3 -hfp3'
  makefile_options['LINKER_FLAGS'] = '-hwp -hpl=obj/lib'
  makefile_options['LIBRARY_FLAGS'] = '-lm'
if args['cxx'] == 'bgxl':
  # suppressed messages:
  #   1500-036:  nostrict option can change semantics
  #   1540-1401: pragma (simd) not recognized
  definitions['COMPILER_CHOICE'] = makefile_options['COMPILER_CHOICE'] = 'bgxlc++'
  makefile_options['PREPROCESSOR_FLAGS'] = ''
  makefile_options['COMPILER_FLAGS'] = \
      '-O3 -qstrict -qlanglvl=extended -qsuppress=1500-036 -qsuppress=1540-1401'
  makefile_options['LINKER_FLAGS'] = ''
  makefile_options['LIBRARY_FLAGS'] = ''

# --std=[name] argument
if args['std'] == 'c++11':
  makefile_options['COMPILER_FLAGS'] = "-std=c++11 " + makefile_options['COMPILER_FLAGS']

# --cvode_path=[path] argument
if args['cvode_path'] != '':
  makefile_options['PREPROCESSOR_FLAGS'] += '-I%s/include' % args['cvode_path']
  makefile_options['LINKER_FLAGS'] += '-L%s/lib' % args['cvode_path']
# -chemistry argument
#TODO: CHEMISTRY_ENABLED and CHEMISTRY_OPTION repeated.
if args['chemistry'] == "gow16":
  definitions['CHEMISTRY_ENABLED'] = '1'
  definitions['CHEMISTRY_OPTION'] = 'INCLUDE_CHEMISTRY'
  definitions['NUM_SPECIES'] = '13'
  #ChemNetwork class header file included in species.hpp
  definitions['CHEMNETWORK_HEADER'] = 'network/gow16.hpp'
  makefile_options['CHEMNET_FILE'] = 'src/chemistry/network/' \
                                      + args['chemistry'] + '.cpp'
  makefile_options['CHEMISTRY_FILE'] = 'src/chemistry/*.cpp'
  makefile_options['LIBRARY_FLAGS'] += ' -lsundials_cvode -lsundials_nvecserial'
elif args['chemistry'] == "gow16_ng":
  definitions['CHEMISTRY_ENABLED'] = '1'
  definitions['CHEMISTRY_OPTION'] = 'INCLUDE_CHEMISTRY'
  definitions['NUM_SPECIES'] = '19'
  definitions['CHEMNETWORK_HEADER'] = 'network/gow16_ng.hpp'
  makefile_options['CHEMNET_FILE'] = 'src/chemistry/network/' \
                                      + args['chemistry'] + '.cpp'
  makefile_options['CHEMISTRY_FILE'] = 'src/chemistry/*.cpp'
  makefile_options['LIBRARY_FLAGS'] += ' -lsundials_cvode -lsundials_nvecserial'
else:
  definitions['CHEMISTRY_OPTION'] = 'NOT_INCLUDE_CHEMISTRY'
  definitions['CHEMISTRY_ENABLED'] = '0'
  definitions['NUM_SPECIES'] = '0'
  makefile_options['CHEMNET_FILE'] = ''
  makefile_options['CHEMISTRY_FILE'] = ''
  definitions['CHEMNETWORK_HEADER'] = 'network/network.hpp'

# -debug argument
if args['debug']:
  definitions['DEBUG'] = 'DEBUG'
  if args['cxx'] == 'g++' or args['cxx'] == 'icc':
    makefile_options['COMPILER_FLAGS'] = '-O0 -g'
  if args['cxx'] == 'cray':
    makefile_options['COMPILER_FLAGS'] = '-O0'
  if args['cxx'] == 'bgxl':
    makefile_options['COMPILER_FLAGS'] = '-O0 -g -qlanglvl=extended'
else:
  definitions['DEBUG'] = 'NOT_DEBUG'

# -mpi argument
if args['mpi']:
  definitions['MPI_OPTION'] = 'MPI_PARALLEL'
  if args['cxx'] == 'g++' or args['cxx'] == 'icc':
    definitions['COMPILER_CHOICE'] = makefile_options['COMPILER_CHOICE'] = 'mpicxx'
  if args['cxx'] == 'cray':
    makefile_options['COMPILER_FLAGS'] += ' -h mpi1'
  if args['cxx'] == 'bgxl':
    definitions['COMPILER_CHOICE'] = makefile_options['COMPILER_CHOICE'] = 'mpixlcxx'
else:
  definitions['MPI_OPTION'] = 'NOT_MPI_PARALLEL'

# -omp argument
if args['omp']:
  definitions['OPENMP_OPTION'] = 'OPENMP_PARALLEL'
  if args['cxx'] == 'g++':
    makefile_options['COMPILER_FLAGS'] += ' -fopenmp'
  if args['cxx'] == 'icc':
    makefile_options['COMPILER_FLAGS'] += ' -openmp'
  if args['cxx'] == 'cray':
    makefile_options['COMPILER_FLAGS'] += ' -homp'
  if args['cxx'] == 'bgxl':
    # use thread-safe version of compiler
    definitions['COMPILER_CHOICE'] += '_r'
    makefile_options['COMPILER_CHOICE'] += '_r'
    makefile_options['COMPILER_FLAGS'] += ' -qsmp'
else:
  definitions['OPENMP_OPTION'] = 'NOT_OPENMP_PARALLEL'
  if args['cxx'] == 'cray':
    makefile_options['COMPILER_FLAGS'] += ' -hnoomp'
  if args['cxx'] == 'icc':
    # suppressed messages:
    #   3180: pragma omp not recognized
    makefile_options['COMPILER_FLAGS'] += ' -diag-disable 3180'

# -hdf5 argument
if args['hdf5']:
  definitions['HDF5_OPTION'] = 'HDF5OUTPUT'
  if args['hdf5_path'] != '':
    makefile_options['PREPROCESSOR_FLAGS'] += '-I%s/include' % args['hdf5_path']
    makefile_options['LINKER_FLAGS'] += '-L%s/lib' % args['hdf5_path']
  if args['cxx'] == 'g++' or args['cxx'] == 'icc' or args['cxx'] == 'cray':
    makefile_options['LIBRARY_FLAGS'] += ' -lhdf5'
  if args['cxx'] == 'bgxl':
    makefile_options['PREPROCESSOR_FLAGS'] += \
        ' -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_BSD_SOURCE' \
        + ' -I/soft/libraries/hdf5/1.10.0/cnk-xl/current/include' \
        + ' -I/bgsys/drivers/ppcfloor/comm/include'
    makefile_options['LINKER_FLAGS'] += \
        ' -L/soft/libraries/hdf5/1.10.0/cnk-xl/current/lib' \
        + ' -L/soft/libraries/alcf/current/xl/ZLIB/lib'
    makefile_options['LIBRARY_FLAGS'] += ' -lhdf5 -lz -lm'
else:
  definitions['HDF5_OPTION'] = 'NO_HDF5OUTPUT'

# Assemble all flags of any sort given to compiler
definitions['COMPILER_FLAGS'] = ' '.join([makefile_options[opt+'_FLAGS'] for opt in \
    ['PREPROCESSOR','COMPILER','LINKER','LIBRARY']])

# -ifov=N argument
definitions['NUM_IFOV'] = str(args['ifov'])

#--- Step 4. Create new files, finish up -----------------------------------------------

# Terminate all filenames with .cpp extension
makefile_options['PROBLEM_FILE'] += '.cpp'
makefile_options['COORDINATES_FILE'] += '.cpp'
makefile_options['EOS_FILE'] += '.cpp'
makefile_options['RSOLVER_FILE'] += '.cpp'
makefile_options['RECONSTRUCT_FILE'] += '.cpp'
makefile_options['HYDRO_INT_FILE'] += '.cpp'

# Read templates
with open(defsfile_input, 'r') as current_file:
  defsfile_template = current_file.read()
with open(makefile_input, 'r') as current_file:
  makefile_template = current_file.read()

# Make substitutions
for key,val in definitions.items():
  defsfile_template = re.sub(r'@{0}@'.format(key), val, defsfile_template)
for key,val in makefile_options.items():
  makefile_template = re.sub(r'@{0}@'.format(key), val, makefile_template)

# Write output files
with open(defsfile_output, 'w') as current_file:
  current_file.write(defsfile_template)
with open(makefile_output, 'w') as current_file:
  current_file.write(makefile_template)

# Finish with diagnostic output
print('Your Athena++ distribution has now been configured with the following options:')
print('  Problem generator:       ' + args['prob'])
print('  Coordinate system:       ' + args['coord'])
print('  Equation of state:       ' + args['eos'])
print('  Riemann solver:          ' + args['flux'])
print('  Reconstruction method:   ' + args['order'])
print('  Hydro integrator:        ' + args['fint'])
print('  Magnetic fields:         ' + ('ON' if args['b'] else 'OFF'))
print('  Special relativity:      ' + ('ON' if args['s'] else 'OFF'))
print('  General relativity:      ' + ('ON' if args['g'] else 'OFF'))
print('  Frame transformations:   ' + ('ON' if args['t'] else 'OFF'))
print('  Chemistry:               ' + (args['chemistry'] if  args['chemistry'] \
        !=  None else 'OFF'))
print('  Post processing:         ' + ('ON' if args['pp'] else 'OFF'))
print('  Compiler and flags:      ' + makefile_options['COMPILER_CHOICE'] + ' ' \
    + makefile_options['PREPROCESSOR_FLAGS'] + ' ' + makefile_options['COMPILER_FLAGS'])
print('  Debug flags:             ' + ('ON' if args['debug'] else 'OFF'))
print('  Linker flags:            ' + makefile_options['LINKER_FLAGS'] + ' ' \
    + makefile_options['LIBRARY_FLAGS'])
print('  MPI parallelism:         ' + ('ON' if args['mpi'] else 'OFF'))
print('  OpenMP parallelism:      ' + ('ON' if args['omp'] else 'OFF'))
print('  HDF5 output:             ' + ('ON' if args['hdf5'] else 'OFF'))
print('  Internal hydro outvars:  ' + str(args['ifov']))
