/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Reducer } from 'redux'

// Constant
import { types } from '../constants/rewards_types'
import { generateQR } from '../qrUtils'

const createWallet = (state: Rewards.State) => {
  state.walletCreated = true
  state.enabledMain = true
  state.enabledContribute = true
  state.createdTimestamp = new Date().getTime()
  chrome.send('brave_rewards.getReconcileStamp', [])
  chrome.send('brave_rewards.getAddresses', [])
  chrome.send('brave_rewards.saveSetting', ['enabledMain', 'true'])
  chrome.send('brave_rewards.saveSetting', ['enabledContribute', 'true'])
  chrome.send('brave_rewards.getContributionAmount', [])

  return state
}

const fetchRewardsInfo = (state: Rewards.State) => {
  chrome.send('brave_rewards.getContributionList')
  chrome.send('brave_rewards.getWalletProperties', [])
  chrome.send('brave_rewards.getPendingContributionsTotal')
  chrome.send('brave_rewards.getBalanceReports')
  chrome.send('brave_rewards.getRecurringTips')
  chrome.send('brave_rewards.getOneTimeTips')
  chrome.send('brave_rewards.getAdsData')

  if (!state.safetyNetFailed) {
    chrome.send('brave_rewards.getGrants', [])
  }
}

const walletReducer: Reducer<Rewards.State | undefined> = (state: Rewards.State, action) => {
  switch (action.type) {
    case types.CREATE_WALLET:
      chrome.send('brave_rewards.createWalletRequested', [])
      break
    case types.WALLET_CREATED:
      state = { ...state }
      state = createWallet(state)
      fetchRewardsInfo(state)
      chrome.send('brave_rewards.saveAdsSetting', ['adsEnabled', 'true'])
      break
    case types.WALLET_CREATE_FAILED:
      state = { ...state }
      state.walletCreateFailed = true
      break
    case types.GET_WALLET_PROPERTIES:
      chrome.send('brave_rewards.getWalletProperties', [])
      break
    case types.ON_WALLET_PROPERTIES:
      {
        state = { ...state }
        let ui = state.ui

        // TODO NZ check why enum can't be used inside Rewards namespace
        if (action.payload.properties.status === 1) {
          ui.walletServerProblem = true
        } else {
          // TODO NZ don't just assign directly
          state.contributionMonthly = action.payload.properties.monthlyAmount
          state.walletInfo = action.payload.properties.wallet
          ui.walletServerProblem = false
        }

        state = {
          ...state,
          ui
        }
        break
      }
    case types.GET_WALLLET_PASSPHRASE:
      chrome.send('brave_rewards.getWalletPassphrase', [])
      break
    case types.ON_WALLLET_PASSPHRASE:
      const value = action.payload.pass
      if (value && value.length > 0) {
        state = { ...state }
        state.recoveryKey = value
      }
      break
    case types.RECOVER_WALLET:
      if (!action.payload.key || action.payload.key.length === 0) {
        let ui = state.ui
        ui.walletRecoverySuccess = false

        state = {
          ...state,
          ui
        }

        break
      }

      chrome.send('brave_rewards.recoverWallet', [action.payload.key])
      break
    case types.ON_RECOVER_WALLET_DATA:
      {
        state = { ...state }
        const result = action.payload.properties.result
        const balance = action.payload.properties.balance
        const grants = action.payload.properties.grants
        let ui = state.ui
        let walletInfo = state.walletInfo

        // TODO NZ check why enum can't be used inside Rewards namespace
        ui.walletRecoverySuccess = result === 0
        if (result === 0) {
          walletInfo.balance = balance
          walletInfo.grants = grants || []
          chrome.send('brave_rewards.getWalletPassphrase', [])
          chrome.send('brave_rewards.getAddresses', [])
          ui.emptyWallet = balance <= 0
          ui.modalBackup = false
        }

        state = {
          ...state,
          ui,
          walletInfo
        }
        break
      }
    case types.ON_ADDRESSES:
      {
        if (!action.payload.addresses) {
          break
        }

        state = { ...state }
        state.addresses = {
          BAT: {
            address: action.payload.addresses.BAT,
            qr: null
          },
          BTC: {
            address: action.payload.addresses.BTC,
            qr: null
          },
          ETH: {
            address: action.payload.addresses.ETH,
            qr: null
          },
          LTC: {
            address: action.payload.addresses.LTC,
            qr: null
          }
        }
        generateQR(action.payload.addresses)
        break
      }
    case types.ON_QR_GENERATED:
      {
        const type = action.payload.type
        if (!type) {
          break
        }

        state = { ...state }
        const addresses = state.addresses

        if (!addresses || !addresses[type] || !addresses[type].address) {
          break
        }

        addresses[type].qr = action.payload.image

        state = {
          ...state,
          addresses
        }
        break
      }
    case types.GET_CURRENT_REPORT:
      {
        chrome.send('brave_rewards.getBalanceReports')
        break
      }
    case types.ON_BALANCE_REPORTS:
      {
        state = { ...state }
        state.reports = action.payload.reports

        const date = new Date()
        const key = `${date.getFullYear()}_${date.getMonth() + 1}`

        // If we have reports for the month we should
        // show the view in the summary, regardless of balance
        // (state.emptyWallet at this time is only used in the report summary)
        if (state.reports &&
            state.reports.hasOwnProperty(key)) {
          state.ui.emptyWallet = false
        }
        break
      }
    case types.CHECK_WALLET_EXISTENCE:
      {
        chrome.send('brave_rewards.checkWalletExistence')
        break
      }
    case types.ON_WALLET_EXISTS:
      {
        state = { ...state }

        if (!action.payload.exists) {
          state.walletCreated = false
          break
        }

        if (!state.walletCreated && action.payload.exists) {
          state = createWallet(state) 
        }

        fetchRewardsInfo(state)

        state.rewardsIntervalId = window.setInterval(() => {
          chrome.send('brave_rewards.getRewardsMainEnabled', [])
          chrome.send('brave_rewards.getWalletProperties', [])
          chrome.send('brave_rewards.getBalanceReports')
        }, 60000)

        break
      }
    case types.ON_CONTRIBUTION_AMOUNT:
      {
        state = { ...state }
        state.contributionMonthly = action.payload.amount
        break
      }
    case types.GET_PENDING_CONTRIBUTION_TOTAL:
      {
        chrome.send('brave_rewards.getPendingContributionsTotal')
        break
      }
    case types.ON_PENDING_CONTRIBUTION_TOTAL:
      {
        state = { ...state }
        state.pendingContributionTotal = action.payload.amount
        break
      }
  }

  return state
}

export default walletReducer
