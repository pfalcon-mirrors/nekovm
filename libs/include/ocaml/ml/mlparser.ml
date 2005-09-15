(*
 *  NekoML Compiler
 *  Copyright (c)2005 Nicolas Cannasse
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *)
 
open Mlast

type error_msg =
	| Unexpected of token
	| Unclosed of string
	| Duplicate_default
	| Unknown_macro of string
	| Invalid_macro_parameters of string * int

exception Error of error_msg * pos

let error_msg = function
	| Unexpected t -> "Unexpected " ^ s_token t
	| Unclosed s -> "Unclosed " ^ s
	| Duplicate_default -> "Duplicate default declaration"
	| Unknown_macro m -> "Unknown macro " ^ m
	| Invalid_macro_parameters (m,n) -> "Invalid number of parameters for macro " ^ m ^ " : " ^ string_of_int n ^ " required"

let error m p = raise (Error (m,p))

let priority = function
	| "=" | "+=" | "-=" | "*=" | "/=" | "|=" | "&=" | "^=" -> -3
	| "&&" | "||" -> -2
	| "==" | "!=" | ">" | "<" | "<=" | ">=" -> -1
	| "+" | "-" -> 0
	| "*" | "/" -> 1
	| "or" | "and" | "xor" -> 2
	| "<<" | ">>" | "%" | ">>>" -> 3
	| _ -> 4

let can_swap _op op =
	let p1 = priority _op in
	let p2 = priority op in
	if p1 < p2 then
		true
	else if p1 = p2 then
		op <> "::"
	else
		false

let rec make_binop op e ((v,p2) as e2) =
	match v with
	| EBinop (_op,_e,_e2) when can_swap _op op ->
		let _e = make_binop op e _e in
		EBinop (_op,_e,_e2) , punion (pos _e) (pos _e2)
	| _ ->
		EBinop (op,e,e2) , punion (pos e) (pos e2)

let rec make_unop op ((v,p2) as e) p1 = 
	match v with
	| EBinop (bop,e,e2) -> EBinop (bop, make_unop op e p1 , e2) , (punion p1 p2)
	| _ ->
		EUnop (op,e), punion p1 p2

let rec make_list p = function
	| [] -> PConstr ([],"[]",None) , p
	| x :: l ->
		let p = snd x in
		let params = PTuple [x;make_list p l] , p in
		PConstr ([],"::",Some params) , p

let is_unop = function
	| "-" | "*" | "!" | "&" -> true
	| _ -> false

let unclosed s p = error (Unclosed s) p

let rec program = parser
	| [< e = expr; p = program >] -> e :: p
	| [< '(Semicolon,_); p = program >] -> p
	| [< '(Eof,_) >] -> []

and expr = parser	
	| [< '(BraceOpen,p1); e = block1; s >] ->
		(match s with parser
		| [< '(BraceClose,p2); s >] -> expr_next (e,punion p1 p2) s
		| [< '(Eof,_) >] -> unclosed "{" p1)
	| [< '(ParentOpen,p1); pl = parameters; s >] ->
		(match s with parser
		| [< '(ParentClose,p2); s >] -> expr_next (ETupleDecl pl,punion p1 p2) s
		| [< '(Eof,_) >] -> unclosed "(" p1)
	| [< '(Keyword Var,p1); '(Const (Ident name),_); t = type_opt; '(Binop "=",_); e = expr; s >] ->
		expr_next (EVar (name,t,e),punion p1 (pos e)) s
	| [< '(Keyword If,p1); cond = expr; e = expr; s >] ->
		(match s with parser
		| [< '(Keyword Else,_); e2 = expr; s >] -> expr_next (EIf (cond,e,Some e2),punion p1 (pos e2)) s
		| [< >] -> expr_next (EIf (cond,e,None),punion p1 (pos e)) s)
	| [< '(Keyword Function,p1); n = ident_opt; '(ParentOpen,po); p = parameter_names; s >] ->
		(match s with parser
		| [< '(ParentClose,_); t = type_opt; e = expr; s >] -> expr_next (EFunction (n,p,e,t),punion p1 (pos e)) s
		| [< '(Eof,_) >] -> unclosed "(" po)
	| [< '(Keyword Type,p1); pl = type_decl_parameters; '(Const (Ident tname),p2); d , p2 = type_declaration p2; s >] ->
		ETypeDecl (pl,tname,d) , punion p1 p2
	| [< '(BracketOpen,p1); b = block; '(BracketClose,p2); s >] ->
		expr_next (EListDecl b , punion p1 p2) s
	| [< '(Keyword Match,p1); e = expr; '(BraceOpen,po); pl = patterns; s >] ->
		(match s with parser
		| [< '(BraceClose,_); s >] -> expr_next (EMatch (e,pl),punion p1 (pos e)) s
		| [< '(Eof,_) >] -> unclosed "{" po)
	| [< '(Binop op,p) when is_unop op; e = expr; s >] ->
		expr_next (make_unop op e p) s
	| [< '(Const (Constr n),p); e = expr_constr n p; s >] ->
		expr_next e s
	| [< '(Const c,p); s >] ->
		expr_next (EConst c,p) s

and expr_next e = parser
	| [< '(Binop ":",_); t , p = type_path; s >] ->
		expr_next (ETypeAnnot (e,t),punion (pos e) p) s
	| [< '(ParentOpen,po); pl = parameters; s >] ->
		(match s with parser
		| [< '(ParentClose,p); s >] -> expr_next (ECall (e,pl),punion (pos e) p) s
		| [< '(Eof,_) >] -> unclosed "(" po)
	| [< '(Dot,_); s >] ->
		(match s with parser
		| [< '(Const (Ident name),p); s >] -> expr_next (EField (e,name),punion (pos e) p) s
		| [< '(BracketOpen,po); e2 = expr; s >] ->
			(match s with parser
			| [< '(BracketClose,p); s >] -> expr_next (EArray (e,e2),punion (pos e) p) s
			| [< '(Eof,_) >] -> unclosed "[" po))
	| [< '(Binop op,_); e2 = expr; s >] ->
		make_binop op e e2
	| [< >] ->
		e

and expr_constr n p = parser
	| [< '(Dot,_); e = expr_constr2 >] -> 
		(match e with
		| EConst ((Ident _) as c) , p2 
		| EConst ((Constr _) as c) , p2 -> EConst (Module ([n],c)) , punion p p2
		| EConst (Module (l,c)) , p2 -> EConst (Module (n :: l,c)) , punion p p2
		| _ -> assert false);
	| [< >] -> EConst (Constr n), p
		
and expr_constr2 = parser
	| [< '(Const (Ident n),p) >] -> EConst (Ident n) , p
	| [< '(Const (Constr n),p); e = expr_constr n p >] -> e

and block1 = parser
	| [< '(Const (Ident name),p); s >] ->
		(match s with parser
		| [< '(Binop "=",_); e = expr; l = record_fields >] -> ERecordDecl ((name,e) :: l)
		| [< e = expr_next (EConst (Ident name),p); b = block >] -> EBlock (e :: b))
	| [< b = block >] ->
		EBlock b

and record_fields = parser
	| [< '(Const (Ident name),_); '(Binop "=",_); e = expr; l = record_fields >] -> (name,e) :: l
	| [< '(Semicolon,_); l = record_fields >] -> l
	| [< >] -> []

and block = parser
	| [< e = expr; b = block >] -> e :: b
	| [< '(Semicolon,_); b = block >] -> b
	| [< >] -> []

and parameter_names = parser
	| [< '(Const (Ident name),_); t = type_opt; p = parameter_names >] -> (name , t) :: p
	| [< '(Comma,_); p = parameter_names >] -> p
	| [< >] -> []

and type_opt = parser
	| [< '(Binop ":",_); t , _ = type_path; >] -> Some t
	| [< >] -> None

and ident_opt = parser
	| [< '(Const (Ident name),_); >] -> Some name
	| [< >] -> None

and parameters = parser
	| [< e = expr; p = parameters_next >] -> e :: p
	| [< >] -> []

and parameters_next = parser
	| [< '(Comma,_); p = parameters >] -> p
	| [< >] -> []

and type_path = parser
	| [< '(Const (Ident tname),p); t = type_path_next (EType (None,[],tname)) p >] -> t
	| [< '(Const (Constr m),p); '(Dot,_); l = type_path_mod; '(Const (Ident tname),_); t = type_path_next (EType (None,m :: l,tname)) p >] -> t
	| [< '(Quote,_); '(Const (Ident a),p); t = type_path_next (EPoly a) p >] -> t
	| [< '(ParentOpen,_); t , p = type_path; l , p = type_path_list_next p; '(ParentClose,_); s >] ->
		type_path_next (ETuple (t :: l)) p s

and type_path_list p = parser
	| [< t , p = type_path; l , p = type_path_list_next p >] -> t :: l , p

and type_path_list_next p = parser
	| [< '(Comma,_); t = type_path_list p >] -> t
	| [< >] -> [] , p

and type_path_next t p = parser
	| [< '(Arrow,_); t2 , p = type_path >] -> 
		(match t2 with
		| EArrow (ta,tb) -> EArrow (EArrow(t,ta),tb) , p
		| _ -> EArrow (t,t2) , p);
	| [< '(Const (Ident tname),p); t = type_path_next (EType (Some t,[],tname)) p >] -> t
	| [< '(Const (Constr m),p); '(Dot,_); l = type_path_mod; '(Const (Ident tname),_); t = type_path_next (EType (Some t,m :: l,tname)) p >] -> t
	| [< >] -> t , p

and type_path_mod = parser
	| [< '(Const (Constr m),_); '(Dot,_); l = type_path_mod >] -> m :: l
	| [< >] -> []

and type_decl_parameters = parser
	| [< '(Quote,_); '(Const (Ident a),_); >] -> [a]
	| [< '(ParentOpen,_); l = type_decl_plist; '(ParentClose,_); >] -> l
	| [< >] -> []

and type_decl_plist = parser
	| [< '(Quote,_); '(Const (Ident a),_); l = type_decl_plist_next >] -> a :: l

and type_decl_plist_next = parser
	| [< '(Comma,_); l = type_decl_plist >] -> l
	| [< >] -> []

and type_declaration p = parser
	| [< '(BraceOpen,_); s >] ->
		(match s with parser
		| [< el , p = record_declaration false >] ->  ERecord el , p
		| [< el , p = union_declaration >] -> EUnion el , p)
	| [< '(Binop "=",_); t , p = type_path >] -> EAlias t , p
	| [< >] -> EAbstract , p

and record_declaration mut = parser
	| [< '(BraceClose,p) >] -> [] , p
	| [< '(Const (Ident "mutable"),_); l = record_declaration true; >] -> l
	| [< '(Semicolon,_); l = record_declaration false >] -> l 
	| [< '(Const (Ident name),_); '(Binop ":",_); t , _ = type_path; l , p = record_declaration false >] -> (name,mut,t) :: l , p

and union_declaration = parser
	| [< '(BraceClose,p) >] -> [] , p
	| [< '(Semicolon,_); l = union_declaration >] -> l 
	| [< '(Const (Constr name),_); t = type_opt; l , p = union_declaration >] -> (name,t) :: l , p

and patterns = parser
	| [< '(Vertical,_); p = pattern; pl = pattern_next; w = when_clause; '(Arrow,_); e = expr; l = patterns >] ->
		(p :: pl,w,e) :: l
	| [< >] -> []

and pattern_next = parser
	| [< '(Vertical,_); p = pattern; l = pattern_next >] ->  p :: l
	| [< >] -> []

and pattern = parser
	| [< d , p = pattern_decl; s >] -> 		
		match s with parser
		| [< '(Const (Ident "as"),_); '(Const (Ident v),p2); s >] -> PAlias (v, (d,p)) , punion p p2
		| [< '(Binop "::",_); d2 , p2 = pattern >] -> PConstr ([],"::",Some (PTuple [(d,p);(d2,p2)] , punion p p2)) , punion p p2
		| [< t = type_opt >] -> 
			match t with
			| None -> d , p
			| Some t -> PTyped ((d , p), t) , p 

and pattern_decl = parser
	| [< '(ParentOpen,p1); pl = pattern_tuple; '(ParentClose,p2) >] -> PTuple pl , punion p1 p2
	| [< '(BraceOpen,p1); '(Const (Ident name),_); '(Binop "=",_); p = pattern; pl = pattern_record; '(BraceClose,p2) >] -> PRecord ((name,p) :: pl) , punion p1 p2
	| [< '(Const (Constr name),p1); l, name, p2 = pattern_mod_path name p1; p , p2 = pattern_opt p2 >] -> PConstr (l,name,p) , punion p1 p2
	| [< '(Const (Ident i),p); >] -> PIdent i , p
	| [< '(Const c,p); >] -> PConst c , p
	| [< '(BracketOpen,p1); l = pattern_list; '(BracketClose,p2) >] -> make_list (punion p1 p2) l

and pattern_mod_path name p = parser
	| [< '(Dot,_); '(Const (Constr n),p); l, n, p = pattern_mod_path n p >] -> name :: l , n , p
	| [< >] -> [], name, p

and pattern_list = parser
	| [< p = pattern; l = pattern_list_next >] -> p :: l
	| [< >] -> []

and pattern_list_next = parser
	| [< '(Semicolon,_); l = pattern_list >] -> l
	| [< >] -> []

and pattern_tuple = parser
	| [< p = pattern; l = pattern_tuple_next >] -> p :: l
	| [< >] -> []

and pattern_tuple_next = parser
	| [< '(Comma,_); l = pattern_tuple >] -> l
	| [< >] -> []

and pattern_record = parser
	| [< '(Const (Ident name),_); '(Binop "=",_); p = pattern; l = pattern_record >] -> (name,p) :: l
	| [< '(Semicolon,_); l = pattern_record >] -> l
	| [< >] -> []

and pattern_opt p = parser
	| [< ( _ , pos as p) = pattern >] -> Some p , pos
	| [< >] -> None , p 

and when_clause = parser
	| [< '(Const (Ident "when"),_); e = expr >] -> Some e
	| [< >] -> None

let parse code file =
	let old = Mllexer.save() in
	Mllexer.init file;
	let last = ref (Eof,null_pos) in
	let rec next_token x =
		let t, p = Mllexer.token code in
		match t with
		| Comment s | CommentLine s -> 
			next_token x
		| _ ->
			last := (t , p);
			Some (t , p)
	in
	try
		let l = program (Stream.from next_token) in
		Mllexer.restore old;
		EBlock l, { pmin = 0; pmax = (pos !last).pmax; pfile = file }
	with
		| Stream.Error _
		| Stream.Failure -> 
			Mllexer.restore old;
			error (Unexpected (fst !last)) (pos !last)
		| e ->
			Mllexer.restore old;
			raise e
