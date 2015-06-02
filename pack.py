import subprocess
import re
import os
import sys
import platform

def split_lines(s):
  return [x.strip() for x in s.split('\n') if len(x.strip())]

def shell(s):
  sys.stderr.write(s + '\n')
  return split_lines(subprocess.check_output(s.split(' ')))

def fatal(msg):
  sys.stderr.write(msg)
  sys.stderr.write('\n')
  sys.exit(1)

def find_libs(kp, system):
  if system == 'Darwin':
    ldd = 'otool -L'
    starting_line = 1
    extension = '.dylib'
  elif system == 'Linux':
    ldd = 'ldd'
    starting_line = 0
    extension = '.so'

  def get_names(path):
    if not os.path.isfile(path):
      print('Warning: ' + path + ' doesn\'t exist. This is fine if that\'s a ' +
        'system library that can expected to be found elsewhere.')
      return None

    libs = shell(ldd + ' ' + path)
    libs = [x for x in libs[starting_line:] if extension in x]
    libs = [x.split(' ')[0] for x in libs]
    libs = [x.split('/')[-1] for x in libs]
    return libs

  all_libs = set(get_names('/'.join(kp)))
  not_found = set()
  while True:
    new_set = set()
    for lib in all_libs:
      deps = get_names('third_party/lib/' + lib)
      if deps is None:
        not_found.add(lib)
      else:
        new_set.update(deps)
    old_len = len(all_libs)
    all_libs.update(new_set)
    if len(all_libs) == old_len:
      break

  for lib in not_found:
    all_libs.remove(lib)

  return list(all_libs)

def find_main_binary():
  find = shell('find bin -name kinesis_producer')
  paths = [f.split('/') for f in find]
  release = [p for p in paths if p[-2] == 'release']

  if len(release) > 1:
    if len(sys.argv) < 2:
      fatal('Error: You appear to have release targets built with more than one ' +
        'toolset, please specify which you want to use as the first argument.\n' +
        'We detected the following:\n' + '\n'.join(['  ' + p[1] for p in release]))
    else:
      release = [p for p in release if p[1].find(sys.argv[1]) == 0]
      if len(release) > 1:
        fatal('The toolset name ' + sys.argv[1] + ' is ambiguous. It matches ' +
          'the following:\n' + '\n'.join(['  ' + p[1] for p in release]))
      elif len(release) == 0:
        fatal('The toolset name ' + sys.argv[1] + ' does not match any folder ' +
          'in ./bin, did you build the release target with that toolset?')
  elif len(release) == 0:
    fatal('Error: You don\'t seem to have a release target built. Build the ' +
      'release target with "./b2 release -j 8 --toolset=...\"')

  return release[0]

def main():
  system = platform.system()
  supported_sys = ['Darwin', 'Linux']
  if not system in supported_sys:
    fatal('Error: Only the following platforms are supported:\n' +
      '\n'.join(supported_sys))

  kp = find_main_binary()
  libs = find_libs(kp, system)

  bin_dir = 'java/amazon-kinesis-producer/src/main/resources/amazon-kinesis-producer-native-binaries/'
  if system == 'Darwin':
    bin_dir += 'osx'
  elif system == 'Linux':
    bin_dir += 'linux'

  shell('rm -rf ' + bin_dir)
  shell('mkdir -p ' + bin_dir)
  shell('cp %s %s/' % ('/'.join(kp), bin_dir))
  for lib in libs:
    shell('cp -L third_party/lib/%s %s/' % (lib, bin_dir))

  files = [lib for lib in libs]
  files.append('kinesis_producer')
  shell('tar cf %s/bin.tar -C %s %s' % (bin_dir, bin_dir, ' '.join(files)))
  shell('rm -f %s' % ' '.join([bin_dir + '/' + f for f in files]))

  print('*' * 80)
  print('Done. Do ' +
    '"pushd java/amazon-kinesis-producer; mvn clean package install; popd"' +
    ' to create and install a new jar containing your updated binaries.')
  print('*' * 80)

if __name__ == "__main__":
  main()
