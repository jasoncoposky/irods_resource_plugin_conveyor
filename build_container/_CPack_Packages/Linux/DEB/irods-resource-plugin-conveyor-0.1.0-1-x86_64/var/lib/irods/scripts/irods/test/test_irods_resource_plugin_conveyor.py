import os
import sys
import unittest
import subprocess
import shutil

# Import iRODS test sessions and library
from . import session
from .. import lib
from ..configuration import IrodsConfig

class Test_Conveyor_Resource(unittest.TestCase):

    def setUp(self):
        super(Test_Conveyor_Resource, self).setUp()
        
        # Use existing admin session
        self.__admin_context = session.make_session_for_existing_admin()
        self.admin = self.__admin_context.__enter__()
        
        self.resc_name = 'conveyor_test_resource'
        self.child_resc_name = 'conveyor_test_child_resource'
        
        # Determine the host and vault path
        self.hostname = lib.get_hostname()
        self.vault_path = os.path.join(self.admin.local_session_dir, 'conveyor_vault')
        self.child_vault_path = os.path.join(self.admin.local_session_dir, 'conveyor_child_vault')
        
        # Ensure a clean state before starting
        self.cleanup_resource_hierarchy()
        self.addCleanup(self.cleanup_resource_hierarchy)

        # Create vault directories
        if not os.path.exists(self.vault_path):
            os.makedirs(self.vault_path)
        if not os.path.exists(self.child_vault_path):
            os.makedirs(self.child_vault_path)

        # Create the child resource
        self.admin.assert_icommand(['iadmin', 'mkresc', self.child_resc_name, 'unixfilesystem', 
                                    f'{self.hostname}:{self.child_vault_path}'], 'STDOUT_SINGLELINE', 'unixfilesystem')
        
        # Create the conveyor resource as a coordinating resource
        self.admin.assert_icommand(['iadmin', 'mkresc', self.resc_name, 'conveyor', ''], 'STDOUT_SINGLELINE', 'conveyor')

        import time
        time.sleep(1) # Wait for catalog

        # Add the child to the conveyor resource
        self.admin.assert_icommand(['iadmin', 'addchildtoresc', self.resc_name, self.child_resc_name])

        # Verify the hierarchy with retries
        import time
        for i in range(5):
            out, err, rc = self.admin.run_icommand(['ilsresc', self.resc_name])
            if self.child_resc_name in out:
                break
            print(f"DEBUG: ilsresc output (attempt {i+1}):\n{out}")
            time.sleep(1)
        else:
             print(f"ERROR: Child resource {self.child_resc_name} not found in hierarchy of {self.resc_name} after retries")

    def tearDown(self):
        self.__admin_context.__exit__(None, None, None)
        super(Test_Conveyor_Resource, self).tearDown()

    def cleanup_resource_hierarchy(self):
        # Try to remove the hierarchy components, ignoring failures if they don't exist
        self.admin.run_icommand(['iadmin', 'rmchildfromresc', self.resc_name, self.child_resc_name])
        self.admin.run_icommand(['iadmin', 'rmresc', self.resc_name])
        self.admin.run_icommand(['iadmin', 'rmresc', self.child_resc_name])
        
        # Clean up vault directories
        if os.path.exists(self.vault_path):
            shutil.rmtree(self.vault_path, ignore_errors=True)
        if os.path.exists(self.child_vault_path):
            shutil.rmtree(self.child_vault_path, ignore_errors=True)

    def test_put_get(self):
        filename = 'test_file.txt'
        filepath = os.path.join(self.admin.local_session_dir, filename)
        content = 'Hello, iRODS Conveyor!'
        
        with open(filepath, 'w') as f:
            f.write(content)

        # Put the file into the conveyor resource
        self.admin.assert_icommand(['iput', '-R', self.resc_name, filepath])

        # Verify the file exists in iRODS
        self.admin.assert_icommand(['ils', '-L', filename], 'STDOUT_SINGLELINE', filename)
        self.admin.assert_icommand(['ils', '-L', filename], 'STDOUT_SINGLELINE', self.resc_name)

        # Get the file back
        output_filepath = filepath + '.out'
        self.admin.assert_icommand(['iget', filename, output_filepath])

        # Verify content
        with open(output_filepath, 'r') as f:
            self.assertEqual(f.read(), content)

        # Clean up local files
        if os.path.exists(filepath):
            os.remove(filepath)
        if os.path.exists(output_filepath):
            os.remove(output_filepath)

    def test_passthrough_behavior(self):
        # This test ensures data actually goes to the child resource
        filename = 'passthru_test.txt'
        filepath = os.path.join(self.admin.local_session_dir, filename)
        content = 'Original Content'
        
        with open(filepath, 'w') as f:
            f.write(content)

        # Put through conveyor
        self.admin.assert_icommand(['iput', '-R', self.resc_name, filepath])

        # Manually verify it's in the child vault
        # iRODS 4.3.0 stores files in <vault>/home/<user>/<filename> usually, 
        # but let's find it via ils -L
        out, err, rc = self.admin.run_icommand(['ils', '-L', filename])
        # Find physical path in output
        # Example: /var/lib/irods/scripts/irods/test/conveyor_child_vault/home/rods/passthru_test.txt
        physical_path = ""
        for line in out.split('\n'):
            if self.child_vault_path in line:
                physical_path = line.strip().split()[0]
                if physical_path == '&': # sometimes ils -L has & before path
                     physical_path = line.strip().split()[1]
                break
        
        self.assertTrue(os.path.exists(physical_path), f"Physical path {physical_path} should exist")

        # Corrupt the data in the vault
        corrupt_content = 'CORRUPTED DATA'
        with open(physical_path, 'w') as f:
            f.write(corrupt_content)

        # Get it back through conveyor - should get the corrupted content if passthrough works
        output_filepath = filepath + '.out'
        self.admin.assert_icommand(['iget', filename, output_filepath])
        
        with open(output_filepath, 'r') as f:
            read_content = f.read()
            self.assertEqual(read_content, corrupt_content)

if __name__ == "__main__":
    unittest.main()
