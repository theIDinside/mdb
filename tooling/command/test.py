import argparse
import json
import re
import subprocess

from .base import Command
from tooling.metadata import BuildMetadata


class TestCommand(Command):
    description = "Run CTest tests by a literal name fragment"
    useMessage = (
        "Run tests from the selected build preset. The name is matched as a literal "
        "substring, so regex syntax is not required. Without --d4 or --d5, run both "
        "DWARF variants and matching unit tests."
    )

    def __init__(self):
        super().__init__("test", commandInstance=self)
        self.parser = argparse.ArgumentParser(
            prog="mdbuild test", description=self.useMessage
        )
        self.parser.add_argument(
            "name", nargs="?", help="Literal substring of a CTest test name"
        )
        variants = self.parser.add_mutually_exclusive_group()
        variants.add_argument(
            "--d4",
            dest="dwarf",
            action="store_const",
            const="D4",
            help="Run only DWARF 4 driver tests",
        )
        variants.add_argument(
            "--d5",
            dest="dwarf",
            action="store_const",
            const="D5",
            help="Run only DWARF 5 driver tests",
        )

        self.parser.add_argument(
            "--verbose",
            action="store_true",
            help="Make ctests be verbose",
        )

    def usage(self):
        self.parser.print_help()

    def validate(self, buildMetadata: BuildMetadata, args) -> argparse.Namespace:
        args = self.parser.parse_args(args)
        if not buildMetadata.getSelected():
            raise ValueError(
                "No build preset is selected. Run `mdbuild select <preset>` first."
            )
        return args

    def runCtest(self, buildDirectory: str, verbose: bool, testRegex: str | None):
        command = ["ctest", "--test-dir", buildDirectory, "--output-on-failure"]
        if verbose:
            command.append("--verbose")
        if testRegex is not None:
            command.extend(["-R", testRegex])
        subprocess.run(command, check=False)

    def run(self, buildMetadata: BuildMetadata, args):
        buildDirectory = buildMetadata.getSelected()
        verbose = args.verbose

        # ./mdbuild test
        if args.name is None and args.dwarf is None:
            return self.runCtest(
                buildDirectory=buildDirectory, verbose=verbose, testRegex=None
            )

        # Resolve literal name fragments and optional DWARF variants against the registry.
        listing = subprocess.run(
            ["ctest", "--test-dir", buildDirectory, "--show-only=json-v1"],
            check=True,
            capture_output=True,
            text=True,
        )
        tests = json.loads(listing.stdout).get("tests", [])
        matches = [
            test["name"]
            for test in tests
            if (args.name is None or args.name in test["name"])
            and (
                args.dwarf is None
                or re.match(rf"^DriverTest\.[^.]+\.{args.dwarf}\.", test["name"])
            )
        ]

        if not matches:
            raise ValueError(
                f"No CTest tests match name {args.name!r} and DWARF variant {args.dwarf or 'any'}."
            )
        if len(matches) > 1:
            print("Matched tests:")
            for name in matches:
                print(f"  {name}")

        # CTest accepts a regex; build it from the registry's exact names, escaping
        # every name so the user-provided fragment is never interpreted as regex.
        # CTest uses its own regex engine, which supports capturing groups but
        # not Python's non-capturing group syntax.
        testRegex = "^(" + "|".join(re.escape(name) for name in matches) + ")$"
        return self.runCtest(
            buildDirectory=buildDirectory, verbose=verbose, testRegex=testRegex
        )


TestCommand()
