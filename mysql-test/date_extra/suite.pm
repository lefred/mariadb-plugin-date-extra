package My::Suite::DateExtra;

@ISA = qw(My::Suite);

return "No date_extra plugin" unless $ENV{DATE_EXTRA_SO};

bless { };
