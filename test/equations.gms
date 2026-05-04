
Equations
  tcost_eq                'total cost accounting equation'
  supply_eq(sl)           'limit on supply available at supply location'
  capacity_eq(wh)         'warehouse capacity'  /a.scale 50, a.l 10, b.m 20/;

* Not supported for now
* Equation Table capacity_eq(wh)  'warehouse capacity'
*       scale    l       m
*    a   50      10
*    b                   20 ;

Equations  obj ;
* obj.. phi  =e=  phipsi + philam + phipi - phieps ;