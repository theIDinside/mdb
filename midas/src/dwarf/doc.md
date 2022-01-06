### Studying material for writing a simlpe DWARF parser

## The DIE

DIE's make up (it seems) the most important core concept of DWARF.

DIE's are represented as "tree's". A DIE can contiain other DIE's, this is used to represent
the static block structure of a source code file.

However, in reality, the DIE's are less trees and more like graphs; for instance, if an enitity in source code
is referencing some other "block structure" somewhere else, the referencing will be cyclical, even though,
that particular DIE is the child of some other DIE.

#### In the object binary though, this abstract tree, is flattened in "prefix order"

If DIE1 has two children, neither of which have any children, it is laid out like this:
DIE_1;(ChildOfDIE1_1,ChildOfDie1_2);

As we can see, ChildOfDIE1_1 and ChildOfDIE1_2 are siblings, siblings are laid out
sequentially after each other, if they have no children. if ChildOfDIE1_1 would have had 2 children
the layout in memory would have been:

`DIE_1;(ChildOfDIE1_1;(ChildOfDIE1_1_1; ChildOfDIE1_1_2; NULL_ENTRY);ChildOfDie1_2; NULL_ENTRY);`

This essentially, forms a tree like structure, something like so:

```
DIE_1;
    ChildOfDIE1_1;
        ChildOfDIE1_1_1; ChildOfDIE1_1_2; NULL_ENTRY
    ChildOfDIE1_2;
    NULL_ENTRY;
DIE_2
    ...
...
```

The DWARF 4.0 further elaborates:

> ... Additional children are represented as siblings of the first child. A chain of sibling entries is
> terminated by a null entry.
> 7.5.3
> In cases where a producer of debugging information feels that it will be important for consumers
> of that information to quickly scan chains of sibling entries, while ignoring the children of
> individual siblings, **that producer may attach a DW_AT_sibling attribute to any debugging
> information entry. The value of this attribute is a reference to the sibling entry of the entry to
> which the attribute is attached.**

This must mean, that for instance; ChildOfDIE1_1 will have an attribute field of { DW_AT_sibling: SomePointerOrReferenceValueTo: ChildOfDIE1_2} to be able to skip over the iteration over ChildOfDIE1_1_1 and ChildOfDIE1_1_2.

## DWARF Expressions
