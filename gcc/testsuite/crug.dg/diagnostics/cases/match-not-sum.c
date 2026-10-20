/* crug-ADT-013: match needs an option, result or payload enum.  */
int f (int x)
{
  match (x)
    {
    some (v) => { return v; }
    none => { return 0; }
    }
}
