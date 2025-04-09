import * as React from "react"
import * as SwitchPrimitives from "@radix-ui/react-switch"
import { cn } from "../../lib/utils"

const Switch = React.forwardRef(({ className, ...props }, ref) => (
  <SwitchPrimitives.Root
    className={cn(
      "peer inline-flex items-center justify-between rounded-full bg-gray-700 data-[state=checked]:bg-green-500 h-6 w-11 relative border-2 border-transparent p-0",
      className
    )}
    {...props}
    ref={ref}
  >
    <span className="sr-only">Toggle</span>
    <SwitchPrimitives.Thumb
      className={cn(
        "bg-white rounded-full h-5 w-5 shadow transform transition-transform data-[state=checked]:translate-x-[18px] data-[state=unchecked]:translate-x-0.5"
      )}
    />
  </SwitchPrimitives.Root>
))

Switch.displayName = SwitchPrimitives.Root.displayName

export { Switch } 