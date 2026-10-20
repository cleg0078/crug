/* crug-ADT-011: a match must have an arm for every variant.  */
int f (option<int> o)
{
  match (o)
    {
    some (v) => { return v; }
    }
}
