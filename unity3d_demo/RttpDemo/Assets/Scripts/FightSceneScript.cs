using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;
using TMPro;

namespace RttpDemo
{
    public class FightSceneScript : MonoBehaviour
    {
        // 服务器通信对象
        private GameNetworkClient m_gameNetworkClient;
        
        public Button m_attackButton;
        public Button m_exitButton;
        public TextMeshProUGUI statusText;


        private string m_attackMessage = "";

        // Use this for initialization
        void Start()
        {
            // 获取GameNetworkClient实例
            m_gameNetworkClient = GameNetworkClient.Instance;
            

            if (m_gameNetworkClient != null)
            {                
                m_gameNetworkClient.m_attackCallback += OnAttackCallback;
            }
            
        }

        // Update is called once per frame
        void Update()
        {
            if (m_gameNetworkClient != null)
            {                
                bool IsConnected = m_gameNetworkClient.IsConnected();
                
                if (IsConnected)
                {                    
                    m_attackButton.interactable = true;
                }                
                else                
                {                    
                    m_attackButton.interactable = false;
                }            
            }
        }

        private void Awake()
        {            
            
        }

        public void OnAttackClick(GameObject sender)
        {
            Debug.Log("attack clicked");

            // 尝试找到骨骼动画对象
            GameObject gameObject = GameObject.Find("Skeleton");
            if (gameObject != null)
            {
                Animator anim = gameObject.GetComponent<Animator>();
                if (anim != null)
                {
                    anim.SetTrigger("AttackTrigger");
                }
            }   
            m_gameNetworkClient.Attack(0, 0);              
        }

        public void OnExitClick(GameObject sender)
        {
            Debug.Log("exit clicked");
            m_gameNetworkClient.CloseConnection();
            // 返回到StartupScene场景
            UnityEngine.SceneManagement.SceneManager.LoadScene("StartupScene");
        }

        void OnAttackCallback(int attackId, long latency)
        {            

               
            m_attackMessage = string.Format("attack: {0}, latency:{1}ms", attackId, latency);            

            Debug.Log(m_attackMessage);

            statusText.text = m_attackMessage;


        }
    }
}