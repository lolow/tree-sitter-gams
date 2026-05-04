*loops

loop(i$(curacc > reltol),
       value(i+1) =  0.5*(value(i) + target / value(i));
       sqrtval    =  value(i);
       curacc     =  abs (value(i)-value(i))/(1+abs(value(i)))
);
loop( (i,j) $ (q(i,j) > 0), x = x + q(i,j));
loop ( i $ (c(i) + c(i)**2), z = z + 1);
loop ( i $ sum(j, abs(q(i,j))), z = z + 1);
loop ( j $ (ord(j) > 1 and ord(j) < card(j)), z = z + 1);
loop ( (i,j) $ k(i,j), y = y + ord(i) + 2*ord(j));
loop(i$(curacc > reltol),
       value(i+1) =  0.5*(value(i) + target/value(i));
       sqrtval    =  value(i+1);
       curacc     =  abs (value(i+1)-value(i))/(1+abs(value(i+1)))
) ;

* Simple if
if (x > 0,
    y = 1;
);

* If with else
if (demand(i) > supply(i),
    shortage(i) = demand(i) - supply(i);
else
    shortage(i) = 0;
);

* If with elseif
if (round(price,2) > 100,
    category = 'expensive';
elseif round(price,2) > 50,
    category = 'moderate';
else
    category = 'cheap';
);

* Nested statements inside if
if (abs(x) > 10,
    y = sqrt(x);
    z = log(x);
elseif x = 0,
    y = 0;
else
    y = -x;
);
