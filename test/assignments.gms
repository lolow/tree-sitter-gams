* Simple scalar assignments
a1 = 10;
a2 = 5;
a3 = -7.5;
a4 = 3.1415;
a5 = 2e3;

* Parameter assignments with index
i(i_yes) = yes;
i(i_no) = no;
cc(j) = bc(j) + 10;
dd(j,k) = aa(j)*bb(k);

* String indices
cc("j1") = 1;
ee("region1","year2025") = 1000;

* Conditional assignments
cc(is_valid(j)) $ is_prime(j) = bc(j) + 10;
ff(j) $ (j > 0) = dd(j); # 2
gg(i,j) $ (i < j) = ii(i,j) - 5;

* Indexed references with ranges
ii(1*5,2*7) = jj(1*5,2*7) + 1;

* Mixed scalar and indexed assignments
calar1 = 100;
param1(j) = 2*cc(j);
* param2(i,j) $ (i.j mod 2 = 0) = 1.5*param1(i);

* Indexed operators
kk(l) $ condition(l) = ll(l) + 5 + sum(l2 $ is_prime(l2), b(l2));
prod(i) $ (i.val > 0) = nn(i)*10 + sum(l2 $ is_prime(l2), b(l2));
d(i) = sum(j, b(i,j));
c    = sum(i, a(i));
d(i) = sum(j$(ord(j) <= 2), b(i,j));
c = sum(i, sum(j, b(i,j)));
c = prod(i, a(i));
c = smax(i, a(i));
c = smin(i, a(i));
d(i) = smax(j, b(i,j));
e(i,j)$(a(i) > 2 and ord(j) == 1) = b(i,j);
c = sum(i, (a(i) + 2)*3 + (sum(j, b(i,j))));
c = sum((i,j),
        a(i)
      + b(i,j)
      + sum(ii$(ord(ii) < ord(i)), a(ii))
      );
c = sum((i,j), a(i) + b(i,j));
c=sum((i,j),a(i)+b(i,j)+2);

* Assignments with complex expressions
pp(j) = qq(j) + rr(j)*2 - ss(j)/3 + tt(j)**2;

* Assignments with immediate strings
uu("some_key") = 42;
vv("another_key") = uu("some_key") + 3;

* Unary functions
x = abs(y - 2*z);
profit = sqrt(revenue(i) - cost(i,j));
ratio = log10(card(setA) + ord(setB));
alpha = exp(beta(k) / gamma);
theta = sin(pi/4) + cos(pi/4);
delta = round(val(param1) / 3.14159);
phi = frac(x(i) + y(j));
omega = sqr(abs(demand(i) - supply(i)));
zeta = sign(price(i) - cost(i));
eta = ceil(quantity(i,j) / 2);
tau = floor((x(i) + y(i)) / 10);
rho = sinh(abs(paramA(k)));
sigma = cosh(log(value(j)));
psi = tanh(exp(-lambda));
chi = arctan(x(i) / y(i));
xi = asin(demand(i) / capacity(i));
nu = acos(cost(i) / revenue(i));
mu = abs(sin(x(i)) * cos(y(j)));

* Multi-arguments built-in functions
lambda = uniform(ord(set1), card(set2));
epsilon = trunc(uniform(0, 1) * 100);
a = uniform(0, 1);
b = round(pi, 3);
c = mod(17, 5);
d = min(x(i), y(j), z(k));
e = max(10, 20, 30);
f = power(2, 8);


* Multi-line assignment
* ww(j) 
* / 
*    "j1" 100,
*    "j2" 200,
*    "j3" 300
* /;

* Conditional with multi-line index set
* xx(j) $ is_valid(j) 
* / 
*    "j1" 10,
*    "j3" 20
* /;
