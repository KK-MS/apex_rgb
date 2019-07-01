#include "../apu_add_apu_process_desc.hpp"
int main ()
{
   int lRetVal = 0;
   apu_add_apu_process_desc  obj;
   obj.Create();
   if (obj.RetErrorState() == 0)
   { 
      if (obj.Resolve() == 0)
      { 
         obj.GenerateProcess();
      }
      else
      { 
         lRetVal = 1;
      }
   }
   else
   { 
      lRetVal = 1;
   }
   return lRetVal;
}
