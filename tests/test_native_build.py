import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'external'))
import native


class NativeBuildTests(unittest.TestCase):
    def fixture(self,root):
        for name in ('CMakeLists.txt','requirements-cmake.txt','external/native.py',
                     'external/project_cmake.py','native/profile_config.hpp'):
            path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('fixture')

    @staticmethod
    def build(command,**kwargs):
        if '--build' in command:
            folder=Path(command[2])/'Release';folder.mkdir()
            (folder/'pinj.exe').write_bytes(b'controller')
            (folder/'pinj_bridge.dll').write_bytes(b'bridge')
        return subprocess.CompletedProcess(command,0)

    def environment(self,root):
        stack=contextlib.ExitStack()
        stack.enter_context(patch.object(native,'ROOT',root))
        stack.enter_context(patch.object(native,'cmake_version',return_value='3.31.6'))
        stack.enter_context(patch.object(native,'configure_command',return_value=['cmake','configure']))
        stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
        return stack

    def test_unchanged_source_reuses_binaries_and_native_changes_get_new_paths(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.fixture(root)
            with self.environment(root),patch.object(native.subprocess,'run',side_effect=self.build) as run:
                first=native.cached_build('cmake',{})
                self.assertEqual(first,native.cached_build('cmake',{}))
                (root/'ui.lua').write_text('UI change')
                self.assertEqual(first,native.cached_build('cmake',{}))
                self.assertEqual(run.call_count,2)
                (root/'native/profile_config.hpp').write_text('new native source')
                second=native.cached_build('cmake',{})
                self.assertNotEqual(first,second)
                self.assertEqual(run.call_count,4)
                self.assertEqual(first[1].read_bytes(),b'bridge')

    def test_damaged_cache_rebuilds_elsewhere_without_replacing_old_dll(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.fixture(root)
            with self.environment(root),patch.object(native.subprocess,'run',side_effect=self.build):
                first=native.cached_build('cmake',{})
                first[1].write_bytes(b'damaged')
                second=native.cached_build('cmake',{})
                self.assertNotEqual(first,second)
                self.assertEqual(first[1].read_bytes(),b'damaged')
                self.assertEqual(second[1].read_bytes(),b'bridge')

    def test_build_failure_keeps_its_log_and_retry_uses_another_directory(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.fixture(root)
            def fail(command,**kwargs):
                kwargs['stdout'].write(b'LINK : fatal error LNK1104\n')
                raise subprocess.CalledProcessError(2,command)
            with self.environment(root),patch.object(native.subprocess,'run',side_effect=fail):
                with self.assertRaisesRegex(ValueError,'Native build failed.*exit 2.*build.log'):
                    native.cached_build('cmake',{})
            log=next((root/'build-external').rglob('build.log'))
            self.assertIn('LNK1104',log.read_text())
            self.assertFalse(list((root/'build-external').glob('cache/*.json')))
            with self.environment(root),patch.object(native.subprocess,'run',side_effect=self.build):
                built=native.cached_build('cmake',{})
            self.assertNotEqual(log.parent,built[0].parent.parent)

    def test_launcher_reports_build_error_without_traceback(self):
        sys.path.insert(0,str(ROOT/'inclient'))
        import launch
        error=io.StringIO()
        with patch.object(sys,'argv',['launch.py']),patch.object(launch,'find_clients',return_value={}),\
             patch.object(launch,'pick',return_value={'pid':1,'target':'client.exe'}),\
             patch.object(launch,'binaries',side_effect=ValueError('Native build failed; see build.log')),\
             contextlib.redirect_stderr(error):
            self.assertEqual(launch.main(),1)
        self.assertIn('Native build failed',error.getvalue())
        self.assertNotIn('Traceback',error.getvalue())


if __name__=='__main__':unittest.main()
