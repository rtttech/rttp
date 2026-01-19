
using System;

namespace RttpDemo
{
public static class TimeUtils
{
    private static readonly DateTime s_epoch = new DateTime(1970, 1, 1);
    
    public static long GetMilliSecondSinceEpoch()
    {
        return (long)(DateTime.UtcNow - s_epoch).TotalMilliseconds;
    }
}
}