# Extension du compilateur MiniJava - Notes d'implementation

Ce projet est base sur le projet modele fourni. Le projet modele implementait deja la chaine principale de compilation :

```text
lexer -> parser -> located AST -> typechecker -> typed AST -> C backend
```

Notre travail etend cette chaine avec de nouvelles constructions syntaxiques MiniJava et applique les modifications de maniere coherente dans les definitions d'AST, le parser, le verificateur de types et le generateur C. Les ajouts principaux sont :

- de nouveaux operateurs binaires : `/`, `>`, `||`, `^`, `|` et `==` ;
- un support interne pour le ET bit-a-bit via `OpBitAnd` / `BITAND` ;
- les boucles `for` ;
- les boucles `do while` ;
- `break` et `continue` ;
- les declarations locales initialisees, par exemple `int i = 0;` ;
- les declarations locales dans la methode `main` ;
- un corps de `main` represente comme une liste d'instructions au lieu d'une seule instruction ;
- le support d'une classe principale declaree avec `public class`.

La boucle `while` existait deja dans le projet modele. Nous ne l'avons donc pas ajoutee depuis zero, mais nous avons modifie son passage dans le verificateur de types afin que le corps d'une boucle soit verifie avec un contexte de boucle. Ce contexte permet de rendre `break` et `continue` valides a l'interieur des boucles, et invalides en dehors.

## Modifications de l'AST

La premiere modification a consiste a etendre la representation du langage. Dans le projet modele, le type des operateurs binaires dans `LMJ.mli` supportait seulement :

```ocaml
OpAdd | OpSub | OpMul | OpLt | OpAnd
```

Nous l'avons etendu avec :

```ocaml
OpDiv
OpGt
OpOr
OpXor
OpBitAnd
OpBitOr
OpEq
```

Ces ajouts permettent de representer les nouveaux operateurs comme de vrais noeuds de l'AST, au lieu de les traiter comme des cas particuliers dans le parser.

Nous avons aussi etendu l'AST des instructions avec :

```ocaml
IFor of identifier * expression * expression * identifier * expression * instruction
IDoWhile of instruction * expression
IReturn of expression
IBreak
IContinue
```

L'AST type dans `TMJ.mli` reprend ces ajouts. La difference principale est que certaines instructions typees transportent les informations de type necessaires au backend C. Par exemple, le `IFor` type stocke les types des variables de boucle :

```ocaml
IFor of identifier * typ * expression * expression * identifier * typ * expression * instruction
```

Ces informations de type permettent au backend de reutiliser la meme logique de cast que pour les affectations normales.

Nous avons aussi modifie la representation du programme. Dans le projet modele, la methode principale etait stockee ainsi :

```ocaml
main: instruction
```

Dans la version finale, elle devient :

```ocaml
main_locals: (identifier * typ) list;
main: instruction list
```

Cette modification etait necessaire parce que `main` peut maintenant contenir des declarations locales et plusieurs instructions de premier niveau, sans obliger tout le corps a etre encapsule dans un bloc artificiel.

## Modifications du lexer et du parser

Dans `lexer.mll`, nous avons ajoute des regles lexicales pour les nouveaux tokens source :

```ocaml
"/"  -> DIV
">"  -> GT
"||" -> OR
"|"  -> BITOR
"^"  -> XOR
"==" -> EQ

"for"      -> FOR
"do"       -> DO
"break"    -> BREAK
"continue" -> CONTINUE
```

Dans `parser.mly`, nous avons declare les nouveaux tokens et ajoute des regles de priorite afin que les expressions melangeant plusieurs operateurs soient parsees correctement :

```ocaml
%left OR
%left AND
%left BITOR
%left XOR
%left BITAND
%left EQ
%nonassoc LT GT
%left PLUS MINUS
%left TIMES DIV
```

Le parser associe chaque nouveau token d'operateur a son constructeur d'AST :

```ocaml
DIV    -> OpDiv
GT     -> OpGt
OR     -> OpOr
XOR    -> OpXor
BITAND -> OpBitAnd
BITOR  -> OpBitOr
EQ     -> OpEq
```

Nous avons aussi modifie la regle `main_class`. La grammaire du projet modele acceptait une classe principale de cette forme :

```java
class Main {
    public static void main(String[] args) {
        instruction
    }
}
```

Le parser final accepte a la fois `class Main` et `public class Main`, et parse le corps de `main` avec `declarations_and_statements` au lieu d'une seule instruction.

Le parser des declarations a ete etendu pour accepter une initialisation :

```ocaml
t = typ id = IDENT ASSIGN e = expression SEMICOLON r = declarations_and_statements
```

Ainsi :

```java
int i = 0;
```

est transforme en deux informations pour le compilateur :

- une declaration locale `(i, int)` ;
- une instruction d'affectation `i = 0;`.

C'est pour cette raison que les declarations initialisees s'integrent naturellement avec l'analyse existante des affectations et des variables initialisees.

Pour le controle de flot, nous avons ajoute des regles de parsing pour :

```java
for (i = start; condition; i = update) statement
do statement while (condition);
break;
continue;
```

La forme de `for` implementee repose sur des affectations. Elle ne parse pas `for (int i = 0; ...)` ; la variable de boucle doit deja etre declaree.

## Modifications du typage

Le verificateur de types est la partie ou se trouve la plupart du travail semantique.

Pour les nouveaux operateurs, nous avons ajoute les regles de typage suivantes :

| Operateur | Types des operandes | Type du resultat |
| --- | --- | --- |
| `/` | `int`, `int` | `int` |
| `>` | `int`, `int` | `boolean` |
| `||` | `boolean`, `boolean` | `boolean` |
| `^` | `boolean`, `boolean` | `boolean` |
| `&` | `int`, `int` | `int` |
| `|` | `int`, `int` | `int` |
| `==` | `int`, `int` ou `boolean`, `boolean` | `boolean` |

`==` est traite separement parce qu'il accepte deux familles possibles d'operandes. Les autres operateurs peuvent etre verifies avec un type attendu unique pour les operandes et un type de resultat unique.

Nous avons modifie `typecheck_instruction` pour lui ajouter deux informations de contexte :

```ocaml
(expected_ret : typ) (in_loop : bool)
```

`expected_ret` est utilise pour `IReturn`, afin qu'une instruction `return` puisse etre verifiee par rapport au type de retour declare de la methode courante.

`in_loop` est utilise pour `break` et `continue`. Ce drapeau est conserve a travers les blocs et les conditionnelles, et il devient `true` lorsque l'on verifie le corps d'un `while`, d'un `for` ou d'un `do while`.

Cela signifie que :

```java
while (cond) {
    break;
}
```

est accepte, mais que :

```java
break;
```

en dehors d'une boucle provoque une erreur de typage.

Le verificateur de `for` effectue plusieurs etapes :

1. Il marque la variable d'initialisation comme initialisee.
2. Il recherche le type de cette variable.
3. Il verifie que l'expression d'initialisation correspond a ce type.
4. Il verifie que la condition de boucle est de type `boolean`.
5. Il recherche le type de la variable mise a jour.
6. Il verifie que l'expression de mise a jour correspond a ce type.
7. Il verifie le corps avec `in_loop = true`.
8. Il produit un `TMJ.IFor` type contenant les noms des variables, leurs types, les expressions typees et le corps type.

L'ensemble des variables initialisees retourne apres une boucle `for` est celui obtenu apres l'initialisation, et non apres le corps. C'est important parce que le corps peut ne jamais s'executer si la condition est fausse des la premiere evaluation.

Le verificateur de `do while` est legerement different :

1. Il verifie d'abord le corps avec `in_loop = true`.
2. Il verifie ensuite la condition.
3. Il utilise l'ensemble des variables initialisees produit par le corps pour verifier la condition.

Cela correspond a la semantique de `do while` : le corps s'execute au moins une fois.

Pour `main`, nous avons ajoute `main_locals` a l'environnement des variables avant de verifier la liste des instructions principales :

```ocaml
let venv = List.fold_left (...) venv p.main_locals
```

Ensuite, nous verifions le corps de `main` avec un nouvel auxiliaire, `typecheck_instruction_list`, parce que `main` n'est plus une seule instruction.

## Modifications du backend C

Dans `mj2c.ml`, nous avons etendu le backend C afin que les nouvelles constructions de l'AST type produisent du vrai code C.

Le backend traduit maintenant directement les nouveaux operateurs :

```ocaml
OpDiv    -> /
OpGt     -> >
OpOr     -> ||
OpXor    -> ^
OpBitAnd -> &
OpBitOr  -> |
OpEq     -> ==
```

Nous avons ajoute la generation de code pour :

```ocaml
TMJ.IFor
TMJ.IDoWhile
TMJ.IReturn
TMJ.IBreak
TMJ.IContinue
```

`IFor` devient :

```c
for (i = start; condition; i = update) body
```

`IDoWhile` devient :

```c
do body while (condition);
```

`IBreak` et `IContinue` sont emis directement :

```c
break;
continue;
```

Pour supporter les variables locales dans `main`, nous avons aussi modifie la construction des informations de classe. Le backend cree maintenant une methode synthetique `main` dans la classe principale synthetique :

```ocaml
methods = [ ("main", { locals = p.main_locals; ... }) ]
```

Cela permet au mecanisme existant de resolution des variables de distinguer les variables locales de `main` des attributs d'objet.

Nous avons aussi ajuste la resolution des variables. Dans le projet modele, un echec de recherche parmi les variables locales ou les parametres pouvait conduire a traiter une variable comme un attribut. Notre version verifie que la variable existe vraiment dans `attribute_info`, et `var2c` retombe sur l'affichage simple du nom de variable si aucune origine d'attribut n'est trouvee. Cela evite d'emettre des acces invalides du type `this->...` pour les variables locales de `main`.

Enfin, `program2c` emet maintenant les `main_locals` comme declarations C dans le `main` genere, avant d'emettre la liste des instructions principales.

## Driver et sortie de debug

Dans `main.ml`, nous avons desactive l'ancien commentaire de debug de l'AST type qui etait ecrit dans les fichiers C generes avant la generation de code :

```ocaml
(* Printf.fprintf output "/*\n";
PrintTMJ.print_program output tmj;
Printf.fprintf output "*/\n"; *)
```

C'est important parce que `TMJ.program.main` est passe d'une instruction unique a une liste d'instructions, tandis que `printTMJ.ml` n'a pas ete adapte a cette nouvelle forme de l'AST type. Le chemin reel de compilation passe donc directement de l'AST type a `Mj2c.program2c`.

Nous avons aussi modifie l'appel au compilateur C pour lui passer :

```bash
-std=c99
```

afin que le C genere soit compile avec un standard explicite.

## Impressions de debug

Les afficheurs de l'AST non type et des tokens ont ete mis a jour uniquement la ou ils sont utiles pour le frontend modifie.

`print_ast.ml` sait maintenant afficher :

```ocaml
OpDiv OpGt OpOr OpXor OpBitAnd OpBitOr OpEq
IFor IDoWhile IReturn IBreak IContinue
```

Il affiche aussi `main` comme une liste d'instructions.

`print_tokens.ml` affiche maintenant les noms des nouveaux tokens :

```ocaml
GT OR XOR DIV BITAND BITOR EQ FOR DO BREAK CONTINUE
```
