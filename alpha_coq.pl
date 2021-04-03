% DCG parser for Peirce's alpha system, accepting a list of
% space-delimited atoms in Coq notation
% John Khoo
% AD MAIOREM DEI GLORIAM

diagram(not(P)) --> ['(', 'not'], diagram(P), [')'].
diagram(and([P|R])) --> ['('], diagram(P), andright(R).
diagram(not(and([not(P)|R]))) --> ['('], diagram(P), orright(R).
diagram(not(and([P, not(Q)]))) --> ['('], diagram(P), ['->'], diagram(Q), [')'].
diagram(not(and(
	[not(and([P, Q])), not(and([not(P), not(Q)]))]
))) --> ['('], diagram(P), ['<->'], diagram(Q), [')'].

diagram(P) --> [P].
diagram(true) --> [].

andright([Q|T]) --> ['/\\'], diagram(Q), andright(T).
andright([Q]) --> ['/\\'], diagram(Q), [')'].
orright([Q|T]) --> ['\\/'], diagram(Q), orright(T).
orright([Q]) --> ['\\/'], diagram(Q), [')'].
