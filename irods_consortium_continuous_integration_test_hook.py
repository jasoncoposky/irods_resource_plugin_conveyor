from __future__ import print_function

import glob
import optparse
import os
import shutil
import subprocess
import logging
import sys

import irods_python_ci_utilities

# --- iRODS CI Test Hook for Conveyor Plugin ---

def get_package_type():
    log = logging.getLogger(__name__)
    import distro
    distro_id = distro.id()
    log.debug('linux distribution detected: {0}'.format(distro_id))
    if distro_id in ['debian', 'ubuntu']:
        pt = 'deb'
    elif distro_id in ['rocky', 'almalinux', 'centos', 'rhel', 'scientific', 'opensuse', 'sles']:
        pt = 'rpm'
    else:
        pt = 'not_detected'
    log.debug('package type detected: {0}'.format(pt))
    return pt

def install_test_prerequisites():
    # install PRC (Python iRODS Client) for tests
    irods_python_ci_utilities.subprocess_get_output(['python3', '-m', 'pip', 'install', 'python-irodsclient'], check_rc=True)

def main():
    parser = optparse.OptionParser()
    parser.add_option('--output_root_directory')
    parser.add_option('--built_packages_root_directory')
    parser.add_option('--test', metavar='dotted name')
    parser.add_option('--skip-setup', action='store_false', dest='do_setup', default=True)
    options, _ = parser.parse_args()

    built_packages_root_directory = options.built_packages_root_directory
    package_suffix = irods_python_ci_utilities.get_package_suffix()
    os_specific_directory = irods_python_ci_utilities.append_os_specific_directory(built_packages_root_directory)

    if options.do_setup:
        # 1. Install libconveyor (Dependency)
        # Note: In a real CI, libconveyor packages should be in the same or linked directory
        irods_python_ci_utilities.install_os_packages_from_files(
            glob.glob(os.path.join(os_specific_directory, f'libconveyor*.{package_suffix}'))
        )

        # 2. Install Conveyor iRODS Plugin
        irods_python_ci_utilities.install_os_packages_from_files(
            glob.glob(os.path.join(os_specific_directory, f'irods-resource-plugin-conveyor*.{package_suffix}'))
        )

        install_test_prerequisites()

    # Default test: standard resource types test which is intensive for coordinating resources
    # and our specific conveyor plugin test suite.
    test = options.test or 'irods.test.test_irods_resource_plugin_conveyor'

    try:
        test_output_file = 'log/test_output.log'
        # Run tests as irods user
        irods_python_ci_utilities.subprocess_get_output(['sudo', 'su', '-', 'irods', '-c',
            f'python3 scripts/run_tests.py --xml_output --run_s {test} 2>&1 | tee {test_output_file}; exit $PIPESTATUS'],
            check_rc=True)

    finally:
        output_root_directory = options.output_root_directory
        if output_root_directory:
            # Gather logs for analysis
            if os.path.exists('/var/lib/irods/log'):
                irods_python_ci_utilities.gather_files_satisfying_predicate('/var/lib/irods/log', output_root_directory, lambda x: True)
            
            # Ensure the specific test output log is copied
            test_log_path = '/var/lib/irods/log/test_output.log'
            if os.path.exists(test_log_path):
                shutil.copy(test_log_path, output_root_directory)
            
            # Copy test reports
            reports_dir = '/var/lib/irods/test-reports'
            if os.path.exists(reports_dir):
                shutil.copytree(reports_dir, os.path.join(output_root_directory, 'test-reports'))


if __name__ == '__main__':
    main()
