/* crug-ADT-001: a payload variant takes a type-name, not empty parens.  */
enum Bad
{
  A (),
  B
};
