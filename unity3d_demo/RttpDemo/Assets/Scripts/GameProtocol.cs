using System;
using System.Runtime.InteropServices;


namespace RttpDemo
{

    /// <summary>
    /// 游戏网络协议定义
    /// 包含客户端和服务端之间通信的报文结构体
    /// </summary>
    public class GameProtocol
    {
        /// <summary>
        /// 报文头部大小（4字节长度 + 4字节类型）
        /// </summary>
        public const int HEADER_SIZE = 8;

        /// <summary>
        /// 报文类型枚举
        /// </summary>
        public enum MessageType : uint
        {
            KeepAlive = 1,     // 心跳包
            Attack = 2,        // 攻击报文
        }

        #region KeepAlive 报文结构体

        /// <summary>
        /// KeepAlive请求报文
        /// </summary>
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct KeepAliveRequest
        {
            /// <summary>
            /// 时间戳
            /// </summary>
            public long Timestamp;

            /// <summary>
            /// 随机数
            /// </summary>
            public int RandomNumber;

            /// <summary>
            /// 转换为字节数组
            /// </summary>
            public byte[] ToBytes()
            {
                int size = Marshal.SizeOf(typeof(KeepAliveRequest));
                byte[] buffer = new byte[size];
                IntPtr ptr = Marshal.AllocHGlobal(size);

                try
                {
                    Marshal.StructureToPtr(this, ptr, true);
                    Marshal.Copy(ptr, buffer, 0, size);
                    return buffer;
                }
                finally
                {
                    Marshal.FreeHGlobal(ptr);
                }
            }

            /// <summary>
            /// 从字节数组转换
            /// </summary>
            public static KeepAliveRequest FromBytes(byte[] bytes)
            {
                int size = Marshal.SizeOf(typeof(KeepAliveRequest));
                IntPtr ptr = Marshal.AllocHGlobal(size);

                try
                {
                    Marshal.Copy(bytes, 0, ptr, size);
                    return (KeepAliveRequest)Marshal.PtrToStructure(ptr, typeof(KeepAliveRequest));
                }
                finally
                {
                    Marshal.FreeHGlobal(ptr);
                }
            }
        }
        #endregion

        #region Attack 报文结构体

        /// <summary>
        /// Attack请求报文
        /// </summary>
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct AttackRequest
        {
            /// <summary>
            /// 攻击ID（唯一标识一次攻击）
            /// </summary>
            public long AttackId;

            /// <summary>
            /// 攻击者ID
            /// </summary>
            public long AttackerId;

            /// <summary>
            /// 目标ID
            /// </summary>
            public long TargetId;

            /// <summary>
            /// 攻击类型
            /// </summary>
            public int AttackType;

            /// <summary>
            /// 攻击位置X坐标
            /// </summary>
            public float PositionX;

            /// <summary>
            /// 攻击位置Y坐标
            /// </summary>
            public float PositionY;

            /// <summary>
            /// 攻击位置Z坐标
            /// </summary>
            public float PositionZ;

            /// <summary>
            /// 攻击时间戳
            /// </summary>
            public long Timestamp;

            /// <summary>
            /// 转换为字节数组
            /// </summary>
            public byte[] ToBytes()
            {
                int size = Marshal.SizeOf(typeof(AttackRequest));
                byte[] buffer = new byte[size];
                IntPtr ptr = Marshal.AllocHGlobal(size);

                try
                {
                    Marshal.StructureToPtr(this, ptr, true);
                    Marshal.Copy(ptr, buffer, 0, size);
                    return buffer;
                }
                finally
                {
                    Marshal.FreeHGlobal(ptr);
                }
            }

            /// <summary>
            /// 从字节数组转换
            /// </summary>
            public static AttackRequest FromBytes(byte[] bytes)
            {
                int size = Marshal.SizeOf(typeof(AttackRequest));
                IntPtr ptr = Marshal.AllocHGlobal(size);

                try
                {
                    Marshal.Copy(bytes, 0, ptr, size);
                    return (AttackRequest)Marshal.PtrToStructure(ptr, typeof(AttackRequest));
                }
                finally
                {
                    Marshal.FreeHGlobal(ptr);
                }
            }
        }

        /// <summary>
        /// Attack响应报文
        /// </summary>
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct AttackResponse
        {
            /// <summary>
            /// 攻击ID（对应请求的攻击ID）
            /// </summary>
            public long AttackId;

            /// <summary>
            /// 攻击结果（0：失败，1：成功）
            /// </summary>
            public int Result;

            /// <summary>
            /// 造成的伤害值
            /// </summary>
            public float Damage;

            /// <summary>
            /// 目标剩余生命值
            /// </summary>
            public float TargetHealth;

            /// <summary>
            /// 服务器时间戳
            /// </summary>
            public long ServerTimestamp;

            /// <summary>
            /// 转换为字节数组
            /// </summary>
            public byte[] ToBytes()
            {
                int size = Marshal.SizeOf(typeof(AttackResponse));
                byte[] buffer = new byte[size];
                IntPtr ptr = Marshal.AllocHGlobal(size);

                try
                {
                    Marshal.StructureToPtr(this, ptr, true);
                    Marshal.Copy(ptr, buffer, 0, size);
                    return buffer;
                }
                finally
                {
                    Marshal.FreeHGlobal(ptr);
                }
            }

            /// <summary>
            /// 从字节数组转换
            /// </summary>
            public static AttackResponse FromBytes(byte[] bytes)
            {
                int size = Marshal.SizeOf(typeof(AttackResponse));
                IntPtr ptr = Marshal.AllocHGlobal(size);

                try
                {
                    Marshal.Copy(bytes, 0, ptr, size);
                    return (AttackResponse)Marshal.PtrToStructure(ptr, typeof(AttackResponse));
                }
                finally
                {
                    Marshal.FreeHGlobal(ptr);
                }
            }
        }
        #endregion

        /// <summary>
        /// 构建完整的报文（包含头部）
        /// </summary>
        /// <param name="type">报文类型</param>
        /// <param name="payload">报文内容</param>
        /// <returns>完整的报文字节数组</returns>
        public static byte[] BuildMessage(MessageType type, byte[] payload = null)
        {
            int payloadSize = payload != null ? payload.Length : 0;
            int totalSize = HEADER_SIZE + payloadSize;

            byte[] message = new byte[totalSize];

            // 写入长度（总长度）
            byte[] sizeBytes = BitConverter.GetBytes(totalSize);
            Buffer.BlockCopy(sizeBytes, 0, message, 0, 4);

            // 写入类型
            byte[] typeBytes = BitConverter.GetBytes((uint)type);
            Buffer.BlockCopy(typeBytes, 0, message, 4, 4);

            // 写入内容（如果有）
            if (payloadSize > 0 && payload != null)
            {
                Buffer.BlockCopy(payload, 0, message, HEADER_SIZE, payloadSize);
            }

            return message;
        }

        /// <summary>
        /// 解析报文头部
        /// </summary>
        /// <param name="header">头部字节数组（至少8字节）</param>
        /// <param name="payloadSize">输出：内容大小</param>
        /// <param name="messageType">输出：报文类型</param>
        /// <returns>解析是否成功</returns>
        public static bool ParseHeader(byte[] header, out int payloadSize, out MessageType messageType)
        {
            payloadSize = 0;
            messageType = MessageType.KeepAlive;

            if (header == null || header.Length < HEADER_SIZE)
            {
                return false;
            }

            // 读取内容大小
            payloadSize = BitConverter.ToInt32(header, 0);

            // 读取报文类型
            messageType = (MessageType)BitConverter.ToUInt32(header, 4);

            return true;
        }
    }

}