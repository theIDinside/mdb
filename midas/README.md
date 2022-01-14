## Functionality List / Todo / Roadmap

A list of functionality I will implement. The commands here, are represented as how they would be input for the REPL
version of the debugger. In the server version, they would obviously be represented differently, possibly by some
own defined protocol of just bytes basically. Or, perhaps it will take a JSON string or something, it is a bit of
overhead that comes with that though. We'll see, regardless, displayed here are the REPL versions of the functionality and
input from the prompt required to run them.

### Breakpoints / watchpoints

Setting breakpoints is done with the `breakpoint` command, which can be abbreviated to `b`

- [x] break at line in file:

```
    b file.cpp:12
```

- [x] break on function:

```
    b foo_bar
```

- [x] break at address:

```
    b 0x4011ff
```

- [ ] set hardware watchpoint
      This command takes the same type / count of parameters as breakpoint.

```
    w foo.id
```

- [ ] conditional breakpoints

### Execution

- [x] continue selected thread

```
    r
```

or

```
    run
```

- [ ] continue thread T

```
    r T
```

- [ ] continue all thread

```
    r --all
```

### Read

- [ ] read memory
- [ ] read register(s)

### Write

- [ ] write to location
