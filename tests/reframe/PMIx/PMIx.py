import reframe as rfm
import reframe.utility.sanity as sn


class SimplePMIxTest(rfm.RegressionTest):
    valid_systems = ["*"]
    valid_prog_environs = ["gnu"]
    sourcesdir = "../../PMIx"
    sourcepath = ""
    build_system = "Make"

    @run_after("init")
    def set_parameters(self):
        self.num_tasks = self.ntasks

    @run_before("run")
    def set_pmix(self):
        self.job.options = [f"-N {self.nnodes}"]
        self.job.launcher.options = ["--mpi=pspmix"]


@rfm.simple_test
class getSizes(SimplePMIxTest):
    executable = "./getSizes"

    ntasks = parameter(range(1, 5))
    nnodes = parameter(range(1, 3))

    @sanity_function
    def validate_output(self):
        names = ["UNIV_SIZE", "JOB_SIZE", "APP_SIZE"]

        num = {}
        for var in names:
            num[var] = sn.len(
                sn.findall(
                    rf"^\[\s?\d+\]: PMIX_{var} is {self.num_tasks}$", self.stdout
                )
            )

        return sn.all(
            [
                # error out should be empty
                sn.assert_found(r"\A\Z", self.stderr),
                # number and value of each variable should match
                sn.all(
                    [
                        sn.assert_eq(
                            num[var], self.num_tasks, f"number of outputs of PMIX_{var}"
                        )
                        for var in names
                    ]
                ),
            ]
        )

        return 0


@rfm.simple_test
class putGet(SimplePMIxTest):
    executable = "./putGet"

    ntasks = parameter([4, 5, 8])
    nnodes = parameter([2, 4])

    @sanity_function
    def validate_output(self):
        prefix = r"\[pspmix_0x[\dabcdef]{12}\[\d+:\d+\]:\d+\]"
        return sn.all(
            [
                # error out should be empty
                sn.assert_found(r"\A\Z", self.stderr),
                # no "failed" should be reported
                sn.assert_not_found(r"failed", self.stdout),
                # number of fence complete messages should match
                sn.assert_eq(
                    sn.len(
                        sn.findall(
                            rf"^{prefix} Fence complete$",
                            self.stdout,
                        )
                    ),
                    self.num_tasks,
                ),
                sn.assert_eq(
                    sn.len(
                        sn.findall(
                            rf"^{prefix} complete and successful$",
                            self.stdout,
                        )
                    ),
                    self.num_tasks,
                ),
            ]
        )

        return 0
