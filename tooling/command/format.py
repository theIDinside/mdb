from .base import Command, Argument
from tooling.metadata import BuildMetadata

import shutil
import subprocess

cppExtensions = (".cpp", ".h")
jsExtensions = (".js",)
# Third party code is never formatted or linted.
excludedDirectories = ("dependencies/", "lib/")


def gitFiles(projectPath, gitArguments):
    output = subprocess.check_output(["git", *gitArguments], cwd=projectPath, text=True)
    return [line for line in output.splitlines() if line]


def sourceFiles(projectPath, allFiles):
    """Files to process: every tracked file, or the files changed (staged, unstaged or untracked) relative to HEAD."""
    if allFiles:
        files = gitFiles(projectPath, ["ls-files"])
    else:
        files = gitFiles(projectPath, ["diff", "--name-only", "--diff-filter=d", "HEAD"])
        files += gitFiles(projectPath, ["ls-files", "--others", "--exclude-standard"])
    files = sorted(set(files))
    return [
        f for f in files if f.endswith(cppExtensions + jsExtensions) and not f.startswith(excludedDirectories)
    ]


def requireTool(name):
    if shutil.which(name) is None:
        raise RuntimeError(f"'{name}' not found in PATH")


def parseOptions(command, args, flags):
    """Splits args into the subset of `flags` that were given. Anything else is an error."""
    given = set()
    for arg in args:
        if arg not in flags:
            command.usage()
            raise ValueError(f"Unknown argument '{arg}'. Valid arguments are: {', '.join(flags)}")
        given.add(arg)
    return given


class FormatCommand(Command):
    description = "Format changed source files (clang-format, prettier)"
    useMessage = (
        "Format C++ files with clang-format and JavaScript files with prettier. By default only the files changed "
        "relative to HEAD (staged, unstaged and untracked) are formatted, which is what the pre-commit hook checks. "
        "'all' formats every tracked file. 'check' only reports files that need formatting and fails if there are any."
    )
    arguments = [
        Argument(
            name="options",
            description="any of [all, check]",
            typeName="string[]",
            type_=list[str],
            required=False,
        )
    ]

    def __init__(self):
        super().__init__("format", commandInstance=self)

    def validate(self, buildMetadata: BuildMetadata, args) -> list:
        return parseOptions(self, args, ["all", "check"])

    def run(self, buildMetadata: BuildMetadata, args):
        projectPath = buildMetadata.getProjectPath()
        files = sourceFiles(projectPath, "all" in args)
        check = "check" in args
        cppFiles = [f for f in files if f.endswith(cppExtensions)]
        jsFiles = [f for f in files if f.endswith(jsExtensions)]
        if not files:
            print("No files to format.")
            return

        unformatted = []
        if cppFiles:
            requireTool("clang-format")
            for f in cppFiles:
                if check:
                    result = subprocess.run(
                        ["clang-format", "--dry-run", "--Werror", f], cwd=projectPath, capture_output=True
                    )
                    if result.returncode != 0:
                        unformatted.append(f)
                else:
                    subprocess.run(["clang-format", "-i", f], cwd=projectPath, check=True)
        if jsFiles:
            requireTool("prettier")
            if check:
                result = subprocess.run(
                    ["prettier", "--list-different", *jsFiles], cwd=projectPath, capture_output=True, text=True
                )
                unformatted += result.stdout.split()
            else:
                subprocess.run(["prettier", "--write", "--log-level", "warn", *jsFiles], cwd=projectPath, check=True)

        if check:
            if unformatted:
                print("Files that need formatting:")
                for f in unformatted:
                    print(f"  {f}")
                raise RuntimeError(f"{len(unformatted)} of {len(files)} files need formatting")
            print(f"All {len(files)} files are formatted.")
        else:
            print(f"Formatted {len(files)} files.")


class LintCommand(Command):
    description = "Run clang-tidy on changed C++ files"
    useMessage = (
        "Run clang-tidy, using the compile commands of the selected preset, on the .cpp files changed relative to "
        "HEAD (staged, unstaged and untracked). 'all' lints every tracked .cpp file, which is slow. 'fix' applies "
        "clang-tidy's fixes."
    )
    arguments = [
        Argument(
            name="options",
            description="any of [all, fix]",
            typeName="string[]",
            type_=list[str],
            required=False,
        )
    ]

    def __init__(self):
        super().__init__("lint", commandInstance=self)

    def validate(self, buildMetadata: BuildMetadata, args) -> list:
        if not buildMetadata.getSelected():
            raise ValueError("No preset selected. Lint needs a configured build for its compile commands.")
        return parseOptions(self, args, ["all", "fix"])

    def run(self, buildMetadata: BuildMetadata, args):
        requireTool("clang-tidy")
        projectPath = buildMetadata.getProjectPath()
        buildDirectory = buildMetadata.getPresetBuildDir(buildMetadata.getSelected())
        # Headers are linted through the .cpp files that include them.
        files = [f for f in sourceFiles(projectPath, "all" in args) if f.endswith(".cpp")]
        if not files:
            print("No files to lint.")
            return
        command = ["clang-tidy", "-p", buildDirectory, "--quiet"]
        if "fix" in args:
            command.append("--fix")
        result = subprocess.run([*command, *files], cwd=projectPath)
        if result.returncode != 0:
            raise RuntimeError(f"clang-tidy failed (exit code {result.returncode})")


FormatCommand()
LintCommand()
