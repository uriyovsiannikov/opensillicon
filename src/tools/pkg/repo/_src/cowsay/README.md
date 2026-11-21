# cowsay

The upstream cowsay 3.8.4 source is retained in `upstream/`. OpenSillicon
uses a native C implementation with text wrapping and common eyes, tongue,
width, and think options; it does not require Perl. `-f` is accepted for
command-line compatibility and currently selects the built-in cow only.
