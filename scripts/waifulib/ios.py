"""Initial iOS foundation build; deliberately excludes the desktop/game targets."""
import subprocess
from waflib import Errors


def options(opt):
    group = opt.add_option_group('iOS bootstrap')
    group.add_option('--ios-target', dest='IOS_TARGET', choices=['simulator', 'device'],
                     help='Build the iOS foundation libraries for simulator or device')
    group.add_option('--ios-min-version', default='15.0',
                     help='Minimum iOS deployment version (default: 15.0)')


def configure(conf):
    sdk = 'iphonesimulator' if conf.options.IOS_TARGET == 'simulator' else 'iphoneos'
    def xcrun(*args):
        try:
            return subprocess.check_output(['xcrun', '--sdk', sdk] + list(args), text=True).strip()
        except (OSError, subprocess.CalledProcessError) as error:
            raise Errors.ConfigurationError('Cannot locate iOS SDK/toolchain: %s' % error)
    version = conf.options.ios_min_version
    import re
    if not re.fullmatch(r'\d+\.\d+(\.\d+)?', version):
        conf.fatal('--ios-min-version must be a version such as 15.0')
    triple = 'arm64-apple-ios' + version
    if sdk == 'iphonesimulator':
        triple += '-simulator'
    flags = ['-target', triple, '-isysroot', xcrun('--show-sdk-path')]
    conf.env.CC = [xcrun('-f', 'clang')]
    conf.env.CXX = [xcrun('-f', 'clang++')]
    conf.env.CFLAGS = flags + ['-O0', '-g', '-fsigned-char']
    conf.env.CXXFLAGS = conf.env.CFLAGS + ['-std=c++11']
    conf.env.LINKFLAGS = flags
    conf.load('compiler_c compiler_cxx subproject')
    conf.env.IOS = True
    conf.env.IOS_SDK = sdk
    conf.env.LIB_ICONV = ['iconv']
    conf.env.DEFINES = ['IOS=1', '_IOS=1', 'POSIX=1', '_POSIX=1',
                        'PLATFORM_POSIX=1', 'GNUC', 'PLATFORM_64BITS=1',
                        'NO_HOOK_MALLOC', 'NO_MEMOVERRIDE_NEW_DELETE',
                        'NDEBUG', '_DLL_EXT=.dylib']
    conf.check_cxx(fragment='''#include <TargetConditionals.h>
        #if !TARGET_OS_IOS
        #error Expected an iOS SDK
        #endif
        static_assert(sizeof(void*) == 8, "Expected ARM64");
        int main() { return 0; }''', msg='Checking iOS ARM64 toolchain')
    conf.add_subproject(['tier0', 'tier1', 'mathlib'])


def build(bld):
    bld.add_subproject(['tier0', 'tier1', 'mathlib'])
