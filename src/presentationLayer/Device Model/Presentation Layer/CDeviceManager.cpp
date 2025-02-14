/*
 * CDeviceManager.cpp
 *
 *  Created on: 8 февр. 2016 г.
 *      Author: alex
 */

#include "CDeviceManager.h"

CDeviceManager* CDeviceManager::ptr = nullptr;
boost::mutex CDeviceManager::mut;
boost::mutex CDeviceManager::queMutex;

CDeviceManager::CDeviceManager(std::vector<settings::DeviceConfig> devConfig) {

	CDeviceFactory&	factory = CDeviceFactory::getFactory();

	for (auto v: devConfig)
	{
		if (v.abstractName.size() == 0) continue;
		if (v.enable == false) continue;

		std::cout << "CDeviceManager constructor: for " << v.abstractName << " started" << std::endl;

		if (v.proto.size() != 0) std::cout << " " << v.proto << std::endl;

		DeviceCtl devCtl;
		boost::shared_ptr<CAbstractDevice> sPtr( factory.deviceFactory(v.abstractName, v.concreteName) );
		devCtl.devInstance = sPtr;

		if (sPtr.get() != nullptr)
		{
			devices.emplace( std::make_pair(v.abstractName, devCtl) );
		}
	}

	std::cout << "CDeviceManager constructor: Created device list:" << std::endl;
	for (auto v: devices)
	{
		std::cout << "- " << v.first << "[" << v.second.devInstance.get() << "]" << std::endl;
	}

}

CDeviceManager::~CDeviceManager() {

}

void CDeviceManager::setCommandToDevice(uint32_t txid, std::string abstractDevice, std::string command,
			std::string parameters, std::string adresat)
{
	boost::mutex::scoped_lock lock(queMutex);

	if (devices.size())
	{
		/*
		 *  Постановка задачи в очередь на отправку команды на абстрактное устройство.
		 */
		// TODO выделить функцию с поиском по имени устройства
		auto it = devices.find(abstractDevice);
		if ( it != devices.end() )
		{

			std::cout << "CDeviceManager::setCommandToDevice: device " << abstractDevice << " found." << std::endl;

			DeviceCtl::Task task;
			task.txId = txid;
			task.adresat = adresat;
			task.abstract = it->second.devInstance->deviceAbstractName();
			task.concrete = it->second.devInstance->deviceConcreteName();

			task.taskFn = boost::bind(sendCommand, devices[abstractDevice].devInstance.get(), command, parameters);

			it->second.taskQue.push(task);

			/*
			 * Если задача в очереди одна, то отправку на устройство сделать незамедлительно
			 */
			std::cout << "INFO! CDeviceManager::setCommandToDevice: " << it->first << " queue has "
					<< it->second.taskQue.size() << " task(s)." << std::endl;
			if (it->second.taskQue.size() == 1)
			{

				std::cout << "CDeviceManager::setCommandToDevice: task added to " << abstractDevice << std::endl;

				/*
				 *  Постановка задачи на отправку команды на абстрактное устройство.
				 *  // TODO Сделать выбор по устрйоствам и выделить в отдельную функцию
				 */
				GlobalThreadPool::get().AddTask(0, task.taskFn);
			}
			else
			{
//				std::cout << "CDeviceManager::setCommandToDevice: " << it->first << " queue has "
//						<< it->second.taskQue.size() << " tasks already." << std::endl;
			}

		}
		else
		{

			std::cout << "CDeviceManager::setCommandToDevice: " << abstractDevice << " not found in device list." << std::endl;

		}

	}
	else
	{
		std::cout << "No instanced devices found" << std::endl;
	}

}

void CDeviceManager::setCommandToClient(setCommandTo::CommandType eventFlag, std::string concreteDevice, std::string command, std::string parameters)
{
	/* TODO
	 * Событие надо просто разослать всем клиентам с помощью постановки задач
	 * Ответ на команду надо послать только адресату
	 */

	boost::mutex::scoped_lock lock(queMutex);

	if ( eventFlag ==  setCommandTo::CommandType::Transaction )
	{
		// Transaction
		DeviceCtl::Task task;
		if ( popDeviceTask(concreteDevice, task) == false )
		{
			// TODO Послать адресату сообщение, что транзакция была потеряна и выйти

			std::cout << "CDeviceManager::setCommandToClient: Transaction lost" << std::endl;
			return;
		}

		auto itAdresat = devices.find(task.adresat);
		if ( itAdresat != devices.end() )
		{

			if (task.adresat == "logic")
			{
				// TODO Set task to adresat
				std::cout << "CDeviceManager::setCommandToClient: Set task for transaction " << task.txId << " to: "
						<< task.adresat << std::endl;

				DeviceCtl::Task clientTask;
				clientTask.txId = task.txId;
				clientTask.adresat = "";
				clientTask.abstract = itAdresat->second.devInstance->deviceAbstractName();
				clientTask.concrete = itAdresat->second.devInstance->deviceConcreteName();

				clientTask.taskFn = boost::bind(sendCommand, devices[task.adresat].devInstance.get(), command, parameters);

				itAdresat->second.taskQue.push(clientTask);

				/*
				 * Если задача в очереди клиента одна, то отправку на устройство сделать незамедлительно
				 */
				std::cout << "INFO! CDeviceManager::setCommandToCient: " << itAdresat->first << " queue has "
						<<  itAdresat->second.taskQue.size() << " task(s)." << std::endl;

				if (itAdresat->second.taskQue.size() == 1)
				{

					std::cout << "CDeviceManager::setCommandToClient: task added to " << itAdresat->first << std::endl;

					/*
					 *  Постановка задачи на отправку команды на абстрактное устройство.
					 */

					GlobalThreadPool::get().AddTask(0, clientTask.taskFn);

				}
				else
				{
//					std::cout << "CDeviceManager::setCommandToClient: " << itAdresat->first << " queue has "
//							<<  itAdresat->second.taskQue.size() << " tasks already." << std::endl;
				}

				/*
				 * Если в очереди есть еще задачи для устройства, то отправить следующую
				 */
				auto it = devices.find(task.abstract);

				std::cout << "INFO! CDeviceManager::setCommandToClient: " << it->first << " queue has "
						<< it->second.taskQue.size() << " task(s)." << std::endl;

				if (it->second.taskQue.size())
				{
					/*
					 *  Постановка задачи на отправку команды на абстрактное устройство.
					 */
					DeviceCtl::Task& newTask = it->second.taskQue.front();
					GlobalThreadPool::get().AddTask(0, newTask.taskFn);

					std::string abstractName = it->second.devInstance.get()->deviceAbstractName();

					std::cout << "CDeviceManager::setCommandToClient: task added to " << abstractName << std::endl;
				}

			}
		}
		else
		{
			std::cout << "CDeviceManager::setCommandToClient: Adresat '" << task.adresat << "'  not found in device list." << std::endl;
		}
	}
	else
	{
		// Event
		// TODO Set task to decision logic to businness logic

		// Event не значит, что транзакция отработана, а наоборот, поэтому очередь девайса не трогаем

		// Отправка команды на бизнес-логику
		auto itAdresat = devices.find(BsnsLogic::s_abstractName);
		if ( itAdresat != devices.end() )
		{
			std::cout << "CDeviceManager::setCommandToClient: Set event to '" << BsnsLogic::s_abstractName <<"' from " << concreteDevice << " as Event." << std::endl;

			DeviceCtl::Task clientTask;
			clientTask.txId = 0;
			clientTask.adresat = "";
			clientTask.abstract = BsnsLogic::s_abstractName;
			clientTask.concrete = HttpClient::s_concreteName;

			clientTask.taskFn = boost::bind(sendCommand, devices[BsnsLogic::s_abstractName].devInstance.get(), command, parameters);

			itAdresat->second.taskQue.push(clientTask);

			/*
			 * Если задача в очереди клиента одна, то отправку на устройство сделать незамедлительно
			 */
			std::cout << "CDeviceManager::setCommandToClient: " << itAdresat->first << " queue has "
					<<  itAdresat->second.taskQue.size() << " task(s)." << std::endl;

			if (itAdresat->second.taskQue.size() == 1)
			{

				std::cout << "CDeviceManager::setCommandToDevice: task added to " << itAdresat->first << std::endl;

				/*
				 *  Постановка задачи на отправку команды на абстрактное устройство.
				 */

				GlobalThreadPool::get().AddTask(0, clientTask.taskFn);

			}
			else
			{
//				std::cout << "CDeviceManager::setCommandToClient: " << itAdresat->first << " queue has "
//						<<  itAdresat->second.taskQue.size() << " tasks already." << std::endl;
			}

		}

		// TODO Set task to pinger

	}

}

void CDeviceManager::ackClient(std::string concreteDevice)
{
	boost::mutex::scoped_lock lock(queMutex);

	DeviceCtl::Task task;
	if ( popDeviceTask(concreteDevice, task) == false )
	{
		// TODO Удивиться, что транзакция клиента была профачена. WTF! It's a BUG!

		std::cout << "ERROR! CDeviceManager::ackClient: Transaction for " << concreteDevice << " lost" << std::endl;
		return;
	}

	std::cout << "CDeviceManager::ackClient: Check queue for '" << concreteDevice << "'" << std::endl;

	// TODO Послать следующую команду из очереди на клиент

	/*
	 * Если в очереди есть еще задачи для устройства, то отправить следующую
	 */
	auto it = devices.begin();

	for (; it != devices.end(); ++it)
	{
		if (it->second.devInstance.get()->deviceConcreteName() == concreteDevice )
		{

			std::string abstractName = it->second.devInstance.get()->deviceAbstractName();

			std::cout << "CDeviceManager::ackClient: " << it->first << " queue has "
					<<  it->second.taskQue.size() << " task(s)." << std::endl;

			if (it->second.taskQue.size())
			{
				/*
				 *  Постановка задачи на отправку команды на абстрактное устройство.
				 */
				DeviceCtl::Task& newTask = it->second.taskQue.front();
				GlobalThreadPool::get().AddTask(0, newTask.taskFn);

				std::cout << "CDeviceManager::ackClient: task added to '" << abstractName << "'" << std::endl;
			}
			else
			{
				std::cout << "CDeviceManager::ackClient: queue of device '" << abstractName << "' is empty." << std::endl;
			}

			break;
		}
	}


}
