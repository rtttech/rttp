using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;
using TMPro;

namespace RttpDemo
{
    public class StartupSceneScript : MonoBehaviour
    {
        // UI元素引用，由StartupSceneUIBuilder动态设置
        public TMP_InputField ipInputField;
        public TMP_InputField portInputField;
        public TextMeshProUGUI statusText;

        public Button connectButton;
        
        
        private GameNetworkClient m_gameNetworkClient;
        
        void Start()
        {            
            Debug.Log("StartupSceneScript Start");

            Screen.orientation = ScreenOrientation.LandscapeRight;


            m_gameNetworkClient = GameNetworkClient.Instance;
            
            if (m_gameNetworkClient == null)
            {
                Debug.LogError("GameNetworkClient instance not found!");
                return;
            }
            
            // 设置默认IP和端口
            ipInputField.text = "127.0.0.1";
            portInputField.text = "9999";
            
            
            statusText.text = "RTTP Version: " + rtttech.RTSocketAPI.rt_get_version();
            
            // 注册GameNetworkClient的事件，不再直接依赖NetworkManager
            m_gameNetworkClient.OnConnectedToServer += OnConnectedToServer;
            m_gameNetworkClient.OnNetworkError += OnConnectionError;
        }
        
        void OnDestroy()
        {            
            if (m_gameNetworkClient != null)
            {
                // 取消注册GameNetworkClient的事件
                m_gameNetworkClient.OnConnectedToServer -= OnConnectedToServer;
                m_gameNetworkClient.OnNetworkError -= OnConnectionError;
            }
        }
        
        public void OnConnectButtonClicked()
        {
            string ip = ipInputField.text;
            int port;
            
            if (string.IsNullOrEmpty(ip))
            {
                statusText.text = "Please input IP";
                return;
            }
            
            if (!int.TryParse(portInputField.text, out port) || port <= 0 || port > 65535)
            {
                statusText.text = "Please input valid port number";
                return;
            }
            
            
            statusText.text = "Connecting...";
            
            // 连接服务器
            m_gameNetworkClient.ConnectToServer(ip, port);
        }
        
        private void OnConnectedToServer()
        {
            statusText.text = "Connect success, loading game...";
            // 延迟一小段时间后切换场景，让玩家看到登录成功的提示
            StartCoroutine(LoadFightSceneAfterDelay(1.0f));
        }
        
        private void OnConnectionError(int errorCode)
        {
            statusText.text = string.Format("error: error code {0}", errorCode);
            
        }
        
        private IEnumerator LoadFightSceneAfterDelay(float delay)
        {
            yield return new WaitForSeconds(delay);
            SceneManager.LoadScene("FightScene");
        }
    }
}