* Parameter declaration
Parameter demand(i) /i1 100, i2 150, i3 200/;

* Scalar declaration
Scalar alpha /scaling factor/;

* * Equation declaration
* Equation cost_equation(i) 'total cost equation'; # a COMMENT

* cost_equation(i) $ (i_model(i)) .. 
*     z = x(i) + alpha * y(i)
* ;

* * Assignment
* alpha = 0.85;

* * Macro declaration with arguments and multiline body
* $macro myMacro(a,b) a + \
* b + \
* 10