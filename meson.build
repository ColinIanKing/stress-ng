project('stress-ng',
    'c',
    version: '0.12.04',
    meson_version: '>=0.53.0',
    default_options: [
        'warning_level=3',
    ],
)

cc = meson.get_compiler('c')
compiler_command = ' '.join(cc.cmd_array())


###############################################################################
# Project configuration

config = configuration_data()

add_project_arguments('-DVERSION="@0@"'.format(meson.project_version()),
    language: 'c',
)

###############################################################################
# Dependencies

math        = cc.find_library('m')
libbsd      = cc.find_library('bsd')
libapparmor = dependency('libapparmor')

apparmor_parser = find_program('apparmor_parser')

# The test subdirectory contains tests to detect presence of features.
subdir('test')


###############################################################################
# Build project

subdir('src')


###############################################################################
# Installed data

subdir('bash-completion')


install_subdir('example-jobs',
    install_dir: get_option('datadir') / meson.project_name()
)


install_man('stress-ng.1')
custom_target('stress-ng.pdf',
    input : 'stress-ng.1',
    output: 'stress-ng.pdf',
    command: [ 'bash', '-c', 'man -t @INPUT@ | ps2pdf -' ],
    capture: true,
)


###############################################################################
# Tests

test('fast',
    find_program('debian/tests/fast-test-all'),
    env: [ 'STRESS_NG=@0@'.format(stress) ]
)

test('lite',
    find_program('debian/tests/lite-test'),
    env: [ 'STRESS_NG=@0@'.format(stress) ]
)

test('slow',
    stress,
    args: [
        '--seq', '0',
        '--timeout', '15',
        '--pathological',
        '--verbose',
        '--times',
        '--tz',
        '--metrics',
    ],
    timeout: 10000000,
    is_parallel: false,
)
