"""Show global variables and check that they do indeed have global scopes."""

from __future__ import print_function



import os, time
import lldb
from lldbsuite.test.lldbtest import *
import lldbsuite.test.lldbutil as lldbutil

class GlobalVariablesTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line number to break inside main().
        self.source = 'main.c'
        self.line = line_number(self.source, '// Set break point at this line.')
        self.shlib_names = ["a"]

    @expectedFailureWindows("llvm.org/pr24764")
    def test(self):
        """Test 'frame variable --scope --no-args' which omits args and shows scopes."""
        self.build()

        # Create a target by the debugger.
        target = self.dbg.CreateTarget("a.out")
        self.assertTrue(target, VALID_TARGET)

        # Break inside the main.
        lldbutil.run_break_set_by_file_and_line (self, self.source, self.line, num_expected_locations=1, loc_exact=True)

        # Register our shared libraries for remote targets so they get automatically uploaded
        environment = self.registerSharedLibrariesWithTarget(target, self.shlib_names)

        # Now launch the process, and do not stop at entry point.
        process = target.LaunchSimple (None, environment, self.get_process_working_directory())
        self.assertTrue(process, PROCESS_IS_VALID)

        # The stop reason of the thread should be breakpoint.
        self.expect("thread list", STOPPED_DUE_TO_BREAKPOINT,
            substrs = ['stopped',
                       'stop reason = breakpoint'])

        # The breakpoint should have a hit count of 1.
        self.expect("breakpoint list -f", BREAKPOINT_HIT_ONCE,
            substrs = [' resolved, hit count = 1'])

        # Check that GLOBAL scopes are indicated for the variables.
        self.expect("frame variable --show-types --scope --show-globals --no-args", VARIABLES_DISPLAYED_CORRECTLY,
            substrs = ['GLOBAL: (int) g_file_global_int = 42',
                       'GLOBAL: (const char *) g_file_global_cstr',
                       '"g_file_global_cstr"',
                       'STATIC: (const char *) g_file_static_cstr',
                       '"g_file_static_cstr"',
                       'GLOBAL: (int) g_common_1 = 21'])

        # 'frame variable' should support address-of operator.
        self.runCmd("frame variable &g_file_global_int")

        # Exercise the 'target variable' command to display globals in a.c file.
        self.expect("target variable g_a", VARIABLES_DISPLAYED_CORRECTLY,
                    substrs = ['g_a', '123'])
        self.expect("target variable g_marked_spot.x", VARIABLES_DISPLAYED_CORRECTLY,
                    substrs = ['g_marked_spot.x', '20'])

        # rdar://problem/9747668
        # runCmd: target variable g_marked_spot.y
        # output: (int) g_marked_spot.y = <a.o[0x214] can't be resolved,  in not currently loaded.
        #         >
        self.expect("target variable g_marked_spot.y", VARIABLES_DISPLAYED_CORRECTLY,
                    substrs = ['g_marked_spot.y', '21'])
        self.expect("target variable g_marked_spot.y", VARIABLES_DISPLAYED_CORRECTLY, matching=False,
                    substrs = ["can't be resolved"])
