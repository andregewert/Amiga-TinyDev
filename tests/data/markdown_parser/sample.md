# Title of the *Sample*

Some text with a [good anchor](#setext-heading), a [bad anchor](#nowhere)
and `code with [a](dead.md) link` in it. An \[escaped](dead.md) one.

Setext heading
--------------

## Größe & `snake_case` ##

See [the other file](other.md), [its section](other.md#a-section),
[a bad section](other.md#missing), [gone](gone.md), ![an image](missing.iff)
and [its page](other.html#a-section). External: [web](http://a.b/c.md),
[mail](mailto:x@y.z), [volume](Work:x.md), [root](/page.md),
[spaced](<other.md>) and [title](other.md "A title").
[up and back](../markdown_parser/other.md) and [encoded](oth%65r.md).

### Repeated

### Repeated

[first](#repeated), [second](#repeated-1), [third](#repeated-2).
[umlaut](#größe--snake_case), [empty]().

[by name][ref], [Collapsed][], [undefined][nothing].

[ref]: other.md
[collapsed]: gone-too.md "Title"

<a name="raw-anchor"></a>
[raw](#raw-anchor)
<!-- [hidden](dead.md)
# Not a heading
-->

```c
# not a heading [either](dead.md)
```

~~~
```
# still code
~~~

    # indented code [link](dead.md)

- item

    # indented in the list
    [in list](gone-list.md)

Paragraph
    # indented, not code

---

Paragraph before a rule
***

```
never closed
