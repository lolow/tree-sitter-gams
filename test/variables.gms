* Variable declaration

Variables
   k(t)      'capital stock (trillion rupees)'
   c(t)      'consumption (trillion rupees per year)'
   i(t)      'investment (trillion rupees per year)'
   utility   'utility measure';

Variables
    u(c,i)  "purchase of domestic materials (mill units per yr)"
    v(c,j)  "imports            (mill tpy)"
    e(c,i)  "exports            (mill tpy)"
    phi     "total cost         (mill us$)"
    phipsi  "raw material cost  (mill us$)";

Free Variables
    phi     "total cost         (mill us$)"
    phipsi  "raw material cost  (mill us$)";

Positive Variables
    u(c,i)  "purchase of domestic materials (mill units per yr)"
    v(c,j)  "imports   (mill typ)"
    e(c,i)  "exports   (mill typ)";

Positive Variables  u, v, e;

Positive Variable x(i) "production level";

Negative Variable y(i) "waste level";

Free Variables z;

Free Variables
    phi     "total cost         (mill us$)"
    phipsi  "raw material cost  (mill us$)";

Positive Variables
    u(c,i)  "purchase of domestic materials (mill units per yr)"
    v(c,j)  "imports   (mill typ)"
    e(c,i)  "exports   (mill typ)";

Variable x1(j) "my first"  / j1.up 10 , j1.Lo 5, j1.l 7, j1.m 0, j1.scale 20 /;
Variable x1(j) "my first"  / j1.UP 10 , j1.LO 5, j1.L 7, j1.M 0, j1.SCALE 20 /;

* Not handled for now
* Variable Table x(i,j) "initial values"
*                             l      m
*     seattle.  new-york      50
*     seattle.  chicago      300
*     san-diego.new-york     275
*     san-diego.chicago           0.009;
