* Parameters declaration

Parameter
    dd(j) "distribution of demand" /  mexico-df   55, guadalaja   15 /;

Parameter
    dd(j) "distribution of demand"
                         /  mexico-df   55,
                            guadalaja   15 /;

Parameter
    a(i)  / seattle  =  350,  san-diego  =  600 /
    b(i)  / seattle    2000,  san-diego    4500 /;

Parameter  hh(j)  /set.j 10/;

* Decided not to consider this notation as # is commentary symbol
* Parameter  gg     /#j    10/;

Parameter
    salaries(employee,manager,department)
        / anderson  .murphy  .toy          = 6000
          hendry    .smith   .toy          = 9000
          hoffman   .morgan  .cosmetics    = 8000 /;

Parameter
    a(row, col)
        /  (row1,row4) . col2*col7    12
            row10      . col10        17
            row1*row7  . col10        33 /;

* Acronyms not handled for now
* Parameter 
*     shutdown(machines) 
*      /   m-1  Tuesday
*          m-2  Wednesday
*          m-3  Friday
*          m-4  Monday
*          m-5  Thursday /;