import json
import re
import subprocess

from .base import Command, Argument
from tooling.metadata import BuildMetadata


class TestCommand(Command):
    description = "Run CTest tests by a literal name fragment"
    useMessage = (
        "Run tests from the selected build preset. The name is matched as a literal "
        "substring, so regex syntax is not required."
    )
    arguments = [
        Argument(
            name="test name",
            description="Literal substring of a CTest test name",
            typeName="string",
            type_=str,
        )
    ]

    def __init__(self):
        super().__init__("test", commandInstance=self)

    def validate(self, buildMetadata: BuildMetadata, args) -> list:
        args = super().validate(buildMetadata, args)
        if not buildMetadata.getSelected():
            raise ValueError(
                "No build preset is selected. Run `mdbuild select <preset>` first."
            )
        return args

    def run(self, buildMetadata: BuildMetadata, args):
        buildDirectory = buildMetadata.getSelected()
        listing = subprocess.run(
            ["ctest", "--test-dir", buildDirectory, "--show-only=json-v1"],
            check=True,
            capture_output=True,
            text=True,
        )
        tests = json.loads(listing.stdout).get("tests", [])
        matches = [test["name"] for test in tests if args[0] in test["name"]]

        if not matches:
            raise ValueError(f"No CTest tests contain '{args[0]}'.")
        if len(matches) > 1:
            print("Matched tests:")
            for name in matches:
                print(f"  {name}")

        # CTest accepts a regex; build it from the registry's exact names, escaping
        # every name so the user-provided fragment is never interpreted as regex.
        # CTest uses its own regex engine, which supports capturing groups but
        # not Python's non-capturing group syntax.
        testRegex = "^(" + "|".join(re.escape(name) for name in matches) + ")$"
        subprocess.run(
            ["ctest", "--test-dir", buildDirectory, "--output-on-failure", "-R", testRegex],
            check=True,
        )


TestCommand()
