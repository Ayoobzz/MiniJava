# Extension du compilateur MiniJava - Notes d'implémentation

Ce projet est basé sur le projet modèle fourni. Le projet modèle implémentait déjà la chaîne principale de compilation :

```text
lexer -> parser -> located AST -> typechecker -> typed AST -> C backend

```

Notre travail étend cette chaîne avec de nouvelles constructions syntaxiques MiniJava et applique les modifications de manière cohérente dans les définitions d'AST, le parser, le vérificateur de types et le générateur C. Les ajouts principaux sont :

* de nouveaux opérateurs binaires : `/`, `>`, `||`, `^`, `|` et `==` ;
* un support interne pour le ET bit-à-bit via `OpBitAnd` / `BITAND` ;
* les boucles `for` ;
* les boucles `do while` ;
* `break` et `continue` ;
* l'instruction `if` sans branche `else` ;
* les types primitifs et littéraux `float` et `String` ;
* l'instruction `return` polymorphe et débridée ;
* la définition et l'implémentation d'`interface` ;
* les déclarations locales initialisées, par exemple `int i = 0;`, traitées par desugaring ;
* les déclarations locales dans la méthode `main` ;
* un corps de `main` représenté comme une liste d'instructions au lieu d'une seule instruction ;
* le support d'une classe principale déclarée avec `public class`.

La boucle `while` existait déjà dans le projet modèle. Nous ne l'avons donc pas ajoutée depuis zéro, mais nous avons modifié son passage dans le vérificateur de types afin que le corps d'une boucle soit vérifié avec un contexte de boucle (`in_loop`). Ce contexte permet de rendre `break` et `continue` valides à l'intérieur des boucles, et provoque une erreur de compilation en dehors.

## Modifications du lexer et du parser

Dans `lexer.mll`, nous avons ajouté des règles lexicales pour capturer :

* Les littéraux `float` (avec suppression automatique du suffixe `f` pour compatibilité Java stricte).
* Les chaînes de caractères.
* Les nouveaux tokens sources :

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

Dans `parser.mly`, nous avons déclaré les nouveaux tokens et ajouté des règles de priorité afin que les expressions mélangeant plusieurs opérateurs soient parsées correctement :

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

Le parser associe chaque nouveau token d'opérateur à son constructeur d'AST. Il résout également plusieurs contraintes structurelles :

* **Dangling-else** : Résolution du conflit shift/reduce via un token de précédence `%prec IFX`.
* **Interfaces** : Ajout des règles de grammaire pour la déclaration d'interfaces et des signatures de méthodes abstraites.

Nous avons modifié la règle `main_class`. La grammaire du projet modèle acceptait une classe principale de cette forme :

```java
class Main {
    public static void main(String[] args) {
        instruction
    }
}

```

Le parser final accepte à la fois `class Main` et `public class Main`, et parse le corps de `main` avec `declarations_and_statements` au lieu d'une seule instruction.

Le parser des déclarations a été étendu pour accepter une initialisation :

```ocaml
t = typ id = IDENT ASSIGN e = expression SEMICOLON r = declarations_and_statements

```

Ainsi, l'initialisation simultanée à la déclaration (`int a = 42;`) est scindée dynamiquement au niveau du parseur (desugaring) en deux informations :

* une déclaration locale `(a, int)` ;
* une instruction d'affectation `a = 42;` en tête de bloc.

Pour le contrôle de flot, nous avons ajouté des règles de parsing pour :

```java
for (i = start; condition; i = update) statement
do statement while (condition);
break;
continue;

```

La forme de `for` implémentée repose sur des affectations. Elle ne parse pas `for (int i = 0; ...)` ; la variable de boucle doit déjà être déclarée.

## Modifications de l'AST

La première modification a consisté à étendre la représentation du langage. Dans le projet modèle, le type des opérateurs binaires dans `LMJ.mli` supportait seulement :

```ocaml
OpAdd | OpSub | OpMul | OpLt | OpAnd

```

Nous l'avons étendu avec :

```ocaml
OpDiv
OpGt
OpOr
OpXor
OpBitAnd
OpBitOr
OpEq

```

Nous avons ajouté les constructeurs `TypFloat`, `TypString`, `ConstFloat` et `ConstString`.

Le type `clas` intègre un attribut booléen `is_interface` et une option `implements`.

Le nœud `metho` a été purgé de son attribut de retour strict en fin de bloc. L'instruction `return` devient une instruction générique évaluable à toute profondeur de l'arbre syntaxique. L'AST des instructions a été étendu avec :

```ocaml
IFor of identifier * expression * expression * identifier * expression * instruction
IDoWhile of instruction * expression
IReturn of expression
IBreak
IContinue

```

L'AST typé dans `TMJ.mli` reprend ces ajouts. La différence principale est que certaines instructions typées transportent les informations de type nécessaires au backend C. Par exemple, le `IFor` typé stocke les types des variables de boucle :

```ocaml
IFor of identifier * typ * expression * expression * identifier * typ * expression * instruction

```

Ces informations de type permettent au backend de réutiliser la même logique de cast que pour les affectations normales.

Nous avons aussi modifié la représentation du programme. Dans le projet modèle, la méthode principale était stockée ainsi :

```ocaml
main: instruction

```

Dans la version finale, elle devient :

```ocaml
main_locals: (identifier * typ) list;
main: instruction list

```

Cette modification était nécessaire parce que `main` peut maintenant contenir des déclarations locales et plusieurs instructions de premier niveau.

## Modifications du typage

Pour les nouveaux opérateurs, nous avons ajouté les règles de typage suivantes :

| Opérateur | Types des opérandes | Type du résultat |
| --- | --- | --- |
| `/` | `int`, `int` ou `float`, `float` | `int` ou `float` |
| `>` | `int`, `int` ou `float`, `float` | `boolean` |
| `\|\|` | `boolean`, `boolean` | `boolean` |
| `^` | `boolean`, `boolean` | `boolean` |
| `&` | `int`, `int` | `int` |
| `\|` | `int`, `int` | `int` |
| `==` | `int`/`int`, `float`/`float`, `boolean`/`boolean` | `boolean` |

Les règles d'inférence de types valident désormais :

* Les opérations arithmétiques sur les flottants et leur incompatibilité croisée stricte avec d'autres types numériques.
* La surcharge de l'instruction `System.out.println` pour autoriser l'affichage de `int`, `float` et `String`.
* La prévention d'instanciation directe d'une `interface` via `new`.
* La résolution complexe de l'opérateur `instanceof` modifiée pour remonter la chaîne d'implémentation (`implements`) des interfaces.

Nous avons modifié `typecheck_instruction` pour lui ajouter deux informations de contexte :

```ocaml
(expected_ret : typ) (in_loop : bool)

```

`expected_ret` est utilisé pour `IReturn`, afin qu'une instruction `return` puisse être vérifiée par rapport au type de retour déclaré de la méthode courante.

`in_loop` est utilisé pour `break` et `continue`. Ce drapeau est conservé à travers les blocs et les conditionnelles, et il devient `true` lorsque l'on vérifie le corps d'un `while`, d'un `for` ou d'un `do while`.

Le vérificateur de `for` effectue plusieurs étapes :

1. Il marque la variable d'initialisation comme initialisée.
2. Il recherche le type de cette variable.
3. Il vérifie que l'expression d'initialisation correspond à ce type.
4. Il vérifie que la condition de boucle est de type `boolean`.
5. Il recherche le type de la variable mise à jour.
6. Il vérifie que l'expression de mise à jour correspond à ce type.
7. Il vérifie le corps avec `in_loop = true`.
8. Il produit un `TMJ.IFor` typé contenant les noms des variables, leurs types, les expressions typées et le corps typé.

L'ensemble des variables initialisées retourné après une boucle `for` est celui obtenu après l'initialisation, et non après le corps.

Le vérificateur de `do while` exécute la logique suivante :

1. Il vérifie d'abord le corps avec `in_loop = true`.
2. Il vérifie ensuite la condition.
3. Il utilise l'ensemble des variables initialisées produit par le corps pour vérifier la condition.

Pour `main`, nous avons ajouté `main_locals` à l'environnement des variables avant de vérifier la liste des instructions principales :

```ocaml
let venv = List.fold_left (...) venv p.main_locals

```

Ensuite, nous vérifions le corps de `main` avec un nouvel auxiliaire, `typecheck_instruction_list`.

## Modifications du backend C

Dans `mj2c.ml`, le backend traduit directement les nouveaux opérateurs :

```ocaml
OpDiv    -> /
OpGt     -> >
OpOr     -> ||
OpXor    -> ^
OpBitAnd -> &
OpBitOr  -> |
OpEq     -> ==

```

Nous avons ajouté la génération de code pour :

* `TMJ.IFor` : `for (i = start; condition; i = update) body`
* `TMJ.IDoWhile` : `do body while (condition);`
* `TMJ.IBreak` : `break;`
* `TMJ.IContinue` : `continue;`
* `TMJ.IReturn` : mappé vers le `return` natif C en forçant un cast pointeur `(void*)` requis par la signature des fonctions compilées.

Le formatage d'affichage C résout dynamiquement les chaînes `printf` via `%g` pour les flottants (suppression des zéros terminaux) et `%s` pour les chaînes.

### Architecture Vtable Sparse

L'implémentation des interfaces détruit l'alignement mémoriel linéaire de l'héritage simple de Java. Pour garantir un dispatch dynamique fonctionnel en C :

* Une table de hachage globale indexe l'intégralité des méthodes du programme en assignant un entier unique par signature.
* La table des méthodes virtuelles (vtable) de chaque classe est générée comme un tableau creux dimensionné selon l'index global, garantissant qu'un appel de méthode via une référence d'interface pointe vers la bonne fonction concrète au runtime. Les interfaces pures sont exclues de la génération d'artefacts C.

### Variables Locales et Compilation

Le compilateur requiert toutes les déclarations de variables au début de la méthode C générée. La fonction `instr2c` prend un paramètre supplémentaire `method_name` pour associer correctement le préfixe de variable locale.

Le backend crée une méthode synthétique `main` dans la classe principale synthétique :

```ocaml
methods = [ ("main", { locals = p.main_locals; ... }) ]

```

Cela permet au mécanisme de résolution des variables de distinguer les variables locales de `main` des attributs d'objet. Une vérification d'existence dans `attribute_info` permet à `var2c` de retomber sur l'affichage simple du nom de variable si aucune origine d'attribut n'est trouvée, évitant d'émettre des accès `this->...` invalides.

`program2c` émet les `main_locals` comme déclarations C dans le `main` généré, avant d'émettre la liste des instructions.

## Driver et sortie de debug

Dans `main.ml`, nous avons désactivé l'ancien commentaire de debug de l'AST typé :

```ocaml
(* Printf.fprintf output "/*\n";
PrintTMJ.print_program output tmj;
Printf.fprintf output "*/\n"; *)

```

`printTMJ.ml` n'ayant pas été adapté au passage de `main` en liste d'instructions, le chemin de compilation passe directement de l'AST typé à `Mj2c.program2c`.

Nous avons modifié l'appel au compilateur C pour lui passer :

```bash
-std=c99

```

`print_ast.ml` affiche désormais `OpDiv OpGt OpOr OpXor OpBitAnd OpBitOr OpEq`, `IFor IDoWhile IReturn IBreak IContinue`, et `main` comme une liste d'instructions.
`print_tokens.ml` affiche les noms des nouveaux tokens : `GT OR XOR DIV BITAND BITOR EQ FOR DO BREAK CONTINUE`.
