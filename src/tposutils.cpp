#include "tposutils.h"
#include "wallet/wallet.h"
#include "utilmoneystr.h"
#include "policy/policy.h"
#include <sstream>
 static const std::string TPOSEXPORTHEADER("TPOSOWNERINFO");
static const int TPOSEXPORTHEADERWIDTH = 40;
 std::string TPoSUtils::PrepareTPoSExportBlock(std::string content)
{
    std::stringstream stream;
     std::string footer(TPOSEXPORTHEADERWIDTH, '=');
    auto header = footer;
    header = header.replace(5, TPOSEXPORTHEADER.size(), TPOSEXPORTHEADER);
    stream << header << std::endl << content << std::endl << footer;
    stream.flush();
     return stream.str();
}
 std::string TPoSUtils::ParseTPoSExportBlock(std::string block)
{
     std::stringstream stream(block);
     std::string header, encodedData, footer;
    stream >> header;
    stream >> encodedData;
    stream >> footer;
     if(!header.empty() && header.size() == footer.size() &&
            header.substr(0, 5 + TPOSEXPORTHEADER.size()) == std::string(5, '=') + TPOSEXPORTHEADER)
    {
        return encodedData;
    }
    else
    {
        return std::string();
    }
}
 bool TPoSUtils::IsTPoSContract(const CTransaction &tx)
{
    return TPoSContract::FromTPoSContractTx(tx).IsValid();
}
 #ifdef ENABLE_WALLET
 bool TPoSUtils::GetTPoSPayments(const CWallet *wallet,
                                const CWalletTx &wtx,
                                CAmount &stakeAmount,
                                CAmount &commissionAmount,
                                CBitcoinAddress &tposAddress)
{
    if(!wtx.tx->IsCoinStake())
        return false;
     CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;
     std::vector<TPoSContract> tposContracts;
     for(auto &&pair : wallet->tposOwnerContracts)
        tposContracts.emplace_back(pair.second);
     for(auto &&pair : wallet->tposMerchantContracts)
        tposContracts.emplace_back(pair.second);
     for(size_t i = 2; i < std::min<size_t>(4, wtx.tx->vout.size()); ++i)
    {
        CTxDestination address;
        if(ExtractDestination(wtx.tx->vout.at(i).scriptPubKey, address))
        {
             CBitcoinAddress tmpAddress(address);
             auto it = std::find_if(std::begin(tposContracts), std::end(tposContracts), [tmpAddress](const TPoSContract &entry) {
                return entry.tposAddress == tmpAddress;
            });
             if(it == std::end(tposContracts))
                continue;
             stakeAmount = wtx.tx->vout[i].nValue;
             // at this moment nNet contains net stake reward
            // commission was sent to merchant address, so it was base of tx
            commissionAmount = nNet;
            // stake amount is just what was sent to tpos address
             tposAddress = tmpAddress;
             return true;
        }
    }
     return false;
 }
 bool TPoSUtils::IsTPoSMerchantContract(CWallet *wallet, const CTransaction &tx)
{
    TPoSContract contract = TPoSContract::FromTPoSContractTx(tx);
    CKeyID keyID;
    contract.merchantAddress.GetKeyID(keyID);
     return contract.IsValid() && wallet->HaveKey(keyID);
}
 bool TPoSUtils::IsTPoSOwnerContract(CWallet *wallet, const CTransaction &tx)
{
    TPoSContract contract = TPoSContract::FromTPoSContractTx(tx);
    auto txDestination = contract.tposAddress.Get();
     class HelperVisitor : public boost::static_visitor<bool>
    {
    private:
        CScriptID &result;
     public:
        HelperVisitor(CScriptID &out) : result(out) {}
         bool operator()(const CKeyID& ) const { return false; }
        bool operator()(const CScriptID& id) const { result = id; return true; }
        bool operator()(const CNoDestination& ) const { return false; }
    };
     CScriptID scriptId;
    boost::apply_visitor(HelperVisitor(scriptId), txDestination);
     return contract.IsValid() &&
            wallet->HaveCScript(scriptId);
}
 std::unique_ptr<CWalletTx> TPoSUtils::CreateTPoSTransaction(CWallet *wallet,
                                                            CReserveKey& reservekey,
                                                            const CScript &tposDestination,
                                                            const CAmount &nValue,
                                                            const CBitcoinAddress &merchantAddress,
                                                            const COutPoint &merchantTxOutPoint,
                                                            int merchantCommission,
                                                            std::string &strError)
{
    std::unique_ptr<CWalletTx> result(new CWalletTx);
    auto &wtxNew = *result;
     CAmount nFeeRequired;
     auto merchantAddrAsStr = merchantAddress.ToString();
     CScript metadataScriptPubKey;
    metadataScriptPubKey << OP_RETURN
                         << (100 - merchantCommission)
                         << std::vector<unsigned char>(merchantAddrAsStr.begin(), merchantAddrAsStr.end())
                         << ParseHex(merchantTxOutPoint.hash.GetHex())
                         << merchantTxOutPoint.n;
     std::stringstream stringStream(metadataScriptPubKey.ToString());
     std::string tokens[5];
     for(auto &token : tokens)
    {
        stringStream >> token;
        std::cout << token << " ";
    }
     std::cout << std::endl;
     std::vector<std::pair<CScript, CAmount>> vecSend {
        { tposDestination, nValue },
        { metadataScriptPubKey, 0 }
    };
     if(wallet->IsLocked())
    {
        strError = "Error: Wallet is locked";
        return nullptr;
    }
 //    if (!wallet->CreateTransaction(vecSend, wtxNew, reservekey, nFeeRequired, strError))
//    {
//        if (nValue + nFeeRequired > wallet->GetBalance())
//            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
//        LogPrintf("Error() : %s\n", strError);
//        return nullptr;
//    }
     std::string reason;
    if(!IsStandardTx(wtxNew, reason))
    {
        strError = strprintf("Error: Not standard tx: %s\n", reason.c_str());
        LogPrintf(strError.c_str());
        return nullptr;
    }
     return result;
}
 #endif
 TPoSContract::TPoSContract(CTransaction tx, COutPoint merchantOutPoint, CBitcoinAddress merchantAddress, CBitcoinAddress tposAddress, short stakePercentage)
{
    this->rawTx = tx;
    this->merchantOutPoint = merchantOutPoint;
    this->tposAddress = tposAddress;
    this->stakePercentage = stakePercentage;
    this->merchantAddress = merchantAddress;
}
 bool TPoSContract::IsValid() const
{
    return !rawTx.IsNull() /*&& !merchantOutPoint.hash.IsNull()*/ &&
            tposAddress.IsValid() && merchantAddress.IsValid() &&
            stakePercentage > 0 && stakePercentage < 100 /*&&
            merchantOutPoint.n >= 0*/;
}
 TPoSContract TPoSContract::FromTPoSContractTx(const CTransaction &tx)
{
    try
    {
        if(tx.vout.size() >= 2)
        {
            const CTxOut *metadataOutPtr = nullptr;
            const CTxOut *tposOutPtr = nullptr;
             for(const CTxOut &txOut : tx.vout)
            {
                if(txOut.scriptPubKey.IsUnspendable())
                    metadataOutPtr = &txOut;
                else if(txOut.scriptPubKey.IsPayToScriptHash())
                    tposOutPtr = &txOut;
            }
             if(metadataOutPtr && tposOutPtr)
            {
                const auto &metadataOut = *metadataOutPtr;
                const auto &tposOut = *tposOutPtr;
                std::vector<std::vector<unsigned char>> vSolutions;
                txnouttype whichType;
                if (Solver(metadataOut.scriptPubKey, whichType, vSolutions) && whichType == TX_NULL_DATA &&
                        tposOut.scriptPubKey.IsPayToScriptHash())
                {
                    // Here we can have a chance that it is transaction which is a tpos contract, let's check if it has
                    std::stringstream stringStream(metadataOut.scriptPubKey.ToString());
                     std::string tokens[5];
                     for(auto &token : tokens)
                    {
                        stringStream >> token;
//                        std::cout << token << " ";
                    }
//                    std::cout << std::endl;
                     auto merhantAddrRaw = ParseHex(tokens[2]);
                    std::string merchantAddrAsStr(merhantAddrRaw.size(), '0');
                     for(size_t i = 0; i < merhantAddrRaw.size(); ++i)
                        merchantAddrAsStr[i] = static_cast<char>(merhantAddrRaw[i]);
                     int commission = std::stoi(tokens[1]);
                    CBitcoinAddress merchantAddress(merchantAddrAsStr);
                    uint256 merchantTxId(ParseHex(tokens[3]));
                    int outIndex = std::stoi(tokens[4]);
                     if(tokens[0] == GetOpName(OP_RETURN) &&
                            commission > 0 && commission < 100 &&
                            merchantAddress.IsValid() /*&& !merchantTxId.IsNull() && outIndex >= 0*/)
                    {
                        if(Solver(tposOut.scriptPubKey, whichType, vSolutions))
                        {
                            CBitcoinAddress tposAddress;
                            tposAddress.Set(CScriptID(uint160(vSolutions[0])));
                             // if we get to this point, it means that we have found tpos contract that was created for us to act as merchant.
                            return TPoSContract(tx, COutPoint(merchantTxId, outIndex), merchantAddress, tposAddress, commission);
                        }
                    }
                }
            }
        }
    }
    catch(std::exception &ex)
    {
        LogPrintf("Failed to parse tpos which had to be tpos, %s\n", ex.what());
    }
     return TPoSContract();
}